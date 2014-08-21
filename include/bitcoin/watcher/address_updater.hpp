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
#ifndef LIBBITCOIN_WATCHER_ADDRESS_UPDATER_HPP
#define LIBBITCOIN_WATCHER_ADDRESS_UPDATER_HPP

#include <bitcoin/watcher/tx_updater.hpp>
#include <bitcoin/client.hpp>
#include <bitcoin/bitcoin.hpp>
#include <unordered_map>

namespace libwallet {

/**
 * Syncs a set of addresses with the bitcoin server.
 */
class BC_API address_updater
  : public bc::client::sleeper
{
public:
    BC_API ~address_updater();
    BC_API address_updater(tx_updater& txu, bc::client::obelisk_codec& codec);

    BC_API void watch(const bc::payment_address& address,
        bc::client::sleep_time poll);

    // Sleeper interface:
    virtual bc::client::sleep_time wakeup();

    //void server_fail(const std::error_code& error);

private:
    // Server queries:
    void query_address(const bc::payment_address& address);

    struct address_row
    {
        libbitcoin::client::sleep_time poll_time;
        std::chrono::steady_clock::time_point last_check;
    };
    std::unordered_map<bc::payment_address, address_row> rows_;

    tx_updater& txu_;
    bc::client::obelisk_codec& codec_;
};

} // namespace libwallet

#endif

