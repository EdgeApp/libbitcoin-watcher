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

BC_API tx_updater::tx_updater(tx_db& db, bc::client::obelisk_codec& codec, send_handler&& on_send)
  : db_(db), codec_(codec),
    on_send_(std::move(on_send)),
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

void tx_updater::watch(bc::hash_digest tx_hash)
{
    if (!db_.has_tx(tx_hash))
        get_tx(tx_hash);
}

void tx_updater::send(bc::transaction_type tx)
{
    auto hash = db_.insert(tx, tx_state::unsent);
    send_tx(hash);
}

std::chrono::milliseconds tx_updater::wakeup()
{
    auto period = std::chrono::seconds(30);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_wakeup_);

    if (period <= elapsed)
    {
        get_height();
        last_wakeup_ = now;
        elapsed = std::chrono::milliseconds::zero();
    }
    return period - elapsed;
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
        server_fail(error);
    };

    auto on_done = [this](size_t height)
    {
        std::cout << "tx_updater::get_height done" << std::endl;
        if (height != db_.last_height())
        {
            db_.at_height(height);

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

        auto hash = db_.insert(tx, tx_state::unconfirmed);
        if (hash != tx_hash)
        {
            auto error = std::make_error_code(std::errc::timed_out);
            server_fail(error);
            return;
        }
        get_index(tx_hash);
    };

    codec_.fetch_transaction(on_error, on_done, tx_hash);
}

void tx_updater::get_tx_mem(bc::hash_digest tx_hash)
{
    auto on_error = [this](const std::error_code& error)
    {
        std::cout << "tx_updater::get_tx_mem error" << std::endl;
        server_fail(error);
    };

    auto on_done = [this, tx_hash](const bc::transaction_type& tx)
    {
        std::cout << "tx_updater::get_tx_mem done" << std::endl;

        auto hash = db_.insert(tx, tx_state::unconfirmed);
        if (hash != tx_hash)
        {
            auto error = std::make_error_code(std::errc::timed_out);
            server_fail(error);
            return;
        }
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

void tx_updater::send_tx(bc::hash_digest tx_hash)
{
    auto on_error = [this, tx_hash](const std::error_code& error)
    {
        //server_fail(error);
        db_.forget(tx_hash);
        on_send_(error, db_.get_tx(tx_hash));
    };

    auto on_done = [this, tx_hash]()
    {
        std::error_code error;
        db_.unconfirmed(tx_hash);
        on_send_(error, db_.get_tx(tx_hash));
    };

    codec_.broadcast_transaction(on_error, on_done, db_.get_tx(tx_hash));
}

} // namespace libwallet

