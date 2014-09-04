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
#include <bitcoin/watcher/address_updater.hpp>

namespace libwallet {

using libbitcoin::client::sleep_time;

BC_API address_updater::~address_updater()
{
}

BC_API address_updater::address_updater(tx_updater& txu, bc::client::obelisk_codec& codec)
  : txu_(txu), codec_(codec)
{
}

void address_updater::watch(const bc::payment_address& address, sleep_time poll)
{
     std::cout << "address_updater::watch " << poll.count() << std::endl;

    // Only insert if it isn't already present:
    rows_[address] = address_row{poll, std::chrono::steady_clock::now()};
    query_address(address);
}

sleep_time address_updater::wakeup()
{
    sleep_time next_wakeup(0);
    auto now = std::chrono::steady_clock::now();

    for (auto& row: rows_)
    {
        auto poll_time = row.second.poll_time;
        auto elapsed = std::chrono::duration_cast<sleep_time>(
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
    return next_wakeup;
}

// - server queries --------------------

void address_updater::query_address(const bc::payment_address& address)
{
    auto on_error = [this](const std::error_code& error)
    {
        std::cout << "address_updater::query_address error" << std::endl;
        txu_.fail(error);
    };

    auto on_done = [this](const bc::blockchain::history_list& history)
    {
        std::cout << "address_updater::query_address done" << std::endl;
        for (auto& row: history)
        {
            txu_.watch(row.output.hash);
            if (row.spend.hash != bc::null_hash)
                txu_.watch(row.spend.hash);
        }
    };

    codec_.address_fetch_history(on_error, on_done, address);
}

} // namespace libwallet

