/*
 * Copyright (c) 2011-2014 libwallet developers (see AUTHORS)
 *
 * This file is part of libbitcoin-client.
 *
 * libbitcoin-client is free software: you can redistribute it and/or modify
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
#ifndef LIBBITCOIN_CLIENT_WATCHER_HPP
#define LIBBITCOIN_CLIENT_WATCHER_HPP

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <obelisk/obelisk.hpp>

#include <bitcoin/address.hpp>
#include <bitcoin/define.hpp>

namespace libwallet {

using namespace libbitcoin;

/**
 * Maintains a connection to an obelisk servers, and uses that connection to
 * watch one or more bitcoin addresses for activity (Actually, since the
 * client library is broken, it opens a new obelisk connection every time.)
 */
class watcher
{
public:
    BC_API ~watcher();
    BC_API watcher();

    BC_API void disconnect();
    BC_API void connect(const std::string& server);

    BC_API void watch_address(const payment_address& address);

    typedef std::function<void (const transaction_type&)> callback;
    BC_API void set_callback(callback& cb);

    watcher(const watcher& copy) = delete;
    watcher& operator=(const watcher& copy) = delete;

private:
    // Guards access to object state:
    std::mutex mutex_;

    /**
     * A pending query to the obelisk server.
     */
    struct obelisk_query {
        enum {
            none, address_history, get_tx
        } type;
        payment_address address;
        size_t from_height;
        hash_digest txid;
    };

    /**
     * A transaction output putting funds into an address. If the spend
     * field is null, this output is unspent.
     */
    struct txo {
        output_point output;
        uint64_t value;
        input_point spend; // null if this output hasn't been spent
    };

    /**
     * An entry in the address table.
     */
    struct address_row {
        size_t last_height;
        std::vector<txo> outputs;
    };

    // Addresses we care about:
    std::unordered_map<payment_address, address_row> addresses_;

    // Transaction table:
    std::unordered_map<hash_digest, transaction_type> tx_table_;

    // Stuff waiting for the query thread:
    size_t last_address_;
    std::queue<hash_digest> tx_query_queue_;

    // Transaction callback:
    callback cb_;

    // Server connection info:
    std::string server_;

    // Obelisk query thread:
    std::atomic<bool> shutdown_;
    std::atomic<bool> request_done_;
    std::thread looper_;

    void enqueue_tx_query(hash_digest hash);
    void history_fetched(const std::error_code& ec,
        const payment_address& address, const blockchain::history_list& history);
    void tx_fetched(const std::error_code& ec, const transaction_type& tx);
    obelisk_query next_query();
    std::string get_server();
    void do_query(const obelisk_query& query);
    void loop();
};

} // namespace libwallet

#endif

