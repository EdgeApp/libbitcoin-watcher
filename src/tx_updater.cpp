/*
 * Copyright (c) 2011-2014 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin-watcher.
 *
 * libbitcoin-watcher is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/watcher/tx_updater.hpp>

namespace libwallet {

using std::placeholders::_1;

BC_API tx_updater::~tx_updater()
{
}

BC_API tx_updater::tx_updater(tx_db& db, bc::client::obelisk_codec& codec,
    tx_callbacks& callbacks)
  : db_(db), codec_(codec),
    callbacks_(callbacks),
    failed_(false),
    queued_get_indices_(0),
    last_wakeup_(std::chrono::steady_clock::now())
{
}

void tx_updater::start()
{
    // Check for new blocks:
    get_height();

    // Handle block-fork checks:
    queue_get_indices();

    // Transmit all unsent transactions:
    db_.foreach_unsent(std::bind(&tx_updater::send_tx, this, _1));
}

void tx_updater::watch(const bc::payment_address& address,
    bc::client::sleep_time poll)
{
     std::cout << "tx_updater::watch " << address.encoded() << " " <<
        poll.count() << std::endl;

    // Only insert if it isn't already present:
    rows_[address] = address_row{poll, std::chrono::steady_clock::now()};
    query_address(address);
}

void tx_updater::send(bc::transaction_type tx)
{
    if (db_.insert(tx, tx_state::unsent))
        callbacks_.on_add(tx);
    send_tx(tx);
}

bc::client::sleep_time tx_updater::wakeup()
{
    bc::client::sleep_time next_wakeup(0);
    auto now = std::chrono::steady_clock::now();

    // Figure out when our next block check is:
    auto period = std::chrono::seconds(30);
    auto elapsed = std::chrono::duration_cast<bc::client::sleep_time>(
        now - last_wakeup_);
    if (period <= elapsed)
    {
        get_height();
        last_wakeup_ = now;
        elapsed = bc::client::sleep_time::zero();
    }
    next_wakeup = period - elapsed;

    // Figure out when our next address check should be:
    for (auto& row: rows_)
    {
        auto poll_time = row.second.poll_time;
        auto elapsed = std::chrono::duration_cast<bc::client::sleep_time>(
            now - row.second.last_check);
        if (poll_time <= elapsed)
        {
            row.second.last_check = now;
            next_wakeup = bc::client::min_sleep(next_wakeup, poll_time);
            query_address(row.first);
        }
        else
            next_wakeup = bc::client::min_sleep(next_wakeup, poll_time - elapsed);
    }

    // Report the last server failure:
    if (failed_)
    {
        callbacks_.on_fail();
        failed_ = false;
    }

    return next_wakeup;
}

void tx_updater::watch(bc::hash_digest tx_hash)
{
    if (!db_.has_tx(tx_hash))
        get_tx(tx_hash);
}

void tx_updater::queue_get_indices()
{
    if (queued_get_indices_)
        return;
    db_.foreach_forked(std::bind(&tx_updater::get_index, this, _1));
}

// - server queries --------------------

void tx_updater::get_height()
{
    auto on_error = [this](const std::error_code& error)
    {
        std::cout << "tx_updater::get_height error" << std::endl;
        (void)error;
        failed_ = true;
    };

    auto on_done = [this](size_t height)
    {
        std::cout << "tx_updater::get_height done" << std::endl;
        if (height != db_.last_height())
        {
            db_.at_height(height);
            callbacks_.on_height(height);

            // Query all unconfirmed transactions:
            db_.foreach_unconfirmed(std::bind(&tx_updater::get_index, this, _1));
            queue_get_indices();
        }
    };

    codec_.fetch_last_height(on_error, on_done);
}

void tx_updater::get_tx(bc::hash_digest tx_hash)
{
    auto on_error = [this, tx_hash](const std::error_code& error)
    {
        std::cout << "tx_updater::get_tx error" << std::endl;
        // A failure means the transaction might be in the mempool:
        (void)error;
        get_tx_mem(tx_hash);
    };

    auto on_done = [this, tx_hash](const bc::transaction_type& tx)
    {
        std::cout << "tx_updater::get_tx done" << std::endl;
        BITCOIN_ASSERT(tx_hash == bc::hash_transaction(tx));
        if (db_.insert(tx, tx_state::unconfirmed))
            callbacks_.on_add(tx);
        get_index(tx_hash);
    };

    codec_.fetch_transaction(on_error, on_done, tx_hash);
}

void tx_updater::get_tx_mem(bc::hash_digest tx_hash)
{
    auto on_error = [this](const std::error_code& error)
    {
        std::cout << "tx_updater::get_tx_mem error" << std::endl;
        (void)error;
        failed_ = true;
    };

    auto on_done = [this, tx_hash](const bc::transaction_type& tx)
    {
        std::cout << "tx_updater::get_tx_mem done" << std::endl;
        BITCOIN_ASSERT(tx_hash == bc::hash_transaction(tx));
        if (db_.insert(tx, tx_state::unconfirmed))
            callbacks_.on_add(tx);
        get_index(tx_hash);
    };

    codec_.fetch_unconfirmed_transaction(on_error, on_done, tx_hash);
}

void tx_updater::get_index(bc::hash_digest tx_hash)
{
    ++queued_get_indices_;

    auto on_error = [this, tx_hash](const std::error_code& error)
    {
        std::cout << "tx_updater::get_index error" << std::endl;

        // A failure means that the transaction is unconfirmed:
        (void)error;
        db_.unconfirmed(tx_hash);

        --queued_get_indices_;
        queue_get_indices();
    };

    auto on_done = [this, tx_hash](size_t block_height, size_t index)
    {
        std::cout << "tx_updater::get_index done" << std::endl;

        // The transaction is confirmed:
        (void)index;

        db_.confirmed(tx_hash, block_height);

        --queued_get_indices_;
        queue_get_indices();
    };

    codec_.fetch_transaction_index(on_error, on_done, tx_hash);
}

void tx_updater::send_tx(const bc::transaction_type& tx)
{
    auto on_error = [this, tx](const std::error_code& error)
    {
        std::cout << "tx_updater::send_tx error" << std::endl;

        //server_fail(error);
        db_.forget(bc::hash_transaction(tx));
        callbacks_.on_send(error, tx);
    };

    auto on_done = [this, tx]()
    {
        std::cout << "tx_updater::send_tx done" << std::endl;

        std::error_code error;
        db_.unconfirmed(bc::hash_transaction(tx));
        callbacks_.on_send(error, tx);
    };

    codec_.broadcast_transaction(on_error, on_done, tx);
}

void tx_updater::query_address(const bc::payment_address& address)
{
    auto on_error = [this](const std::error_code& error)
    {
        std::cout << "address_updater::query_address error" << std::endl;
        (void)error;
        failed_ = true;
    };

    auto on_done = [this](const bc::blockchain::history_list& history)
    {
        std::cout << "address_updater::query_address done" << std::endl;
        for (auto& row: history)
        {
            watch(row.output.hash);
            if (row.spend.hash != bc::null_hash)
                watch(row.spend.hash);
        }
    };

    codec_.address_fetch_history(on_error, on_done, address);
}

} // namespace libwallet

