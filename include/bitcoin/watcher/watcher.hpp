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
#ifndef LIBBITCOIN_WATCHER_WATCHER_HPP
#define LIBBITCOIN_WATCHER_WATCHER_HPP

#include <bitcoin/watcher/address_updater.hpp>
#include <unordered_set>
#include <bitcoin/client.hpp>
#include <zmq.hpp>

namespace libwallet {

using namespace libbitcoin;

/**
 * Maintains a connection to an obelisk servers, and uses that connection to
 * watch one or more bitcoin addresses for activity (Actually, since the
 * client library is broken, it opens a new obelisk connection every time.)
 */
class BC_API watcher
{
public:
    BC_API ~watcher();
    BC_API watcher();

    // - Server: -----------------------
    BC_API void disconnect();
    BC_API void connect(const std::string& server);

    // - Serialization: ----------------
    BC_API data_chunk serialize();
    BC_API bool load(const data_chunk& data);

    // - Addresses: --------------------
    BC_API void watch_address(const payment_address& address);
    BC_API void prioritize_address(const payment_address& address);

    // - Transactions: -----------------
    BC_API void send_tx(const transaction_type& tx);
    BC_API transaction_type find_tx(hash_digest txid);
    BC_API bool get_tx_height(hash_digest txid, int& height);
    BC_API output_info_list get_utxos(const payment_address& address);

    typedef std::function<void (const transaction_type&)> callback;
    BC_API void set_callback(callback& cb);

    typedef std::function<void (std::error_code, const transaction_type&)> tx_sent_callback;
    BC_API void set_tx_sent_callback(tx_sent_callback& cb);

    // - Chain height: -----------------
    BC_API size_t get_last_block_height();

    typedef std::function<void (const size_t)> block_height_callback;
    BC_API void set_height_callback(block_height_callback& cb);

    // - Status queries: ---------------
    typedef enum {
        watcher_sync_ok = 0,
        watcher_syncing
    } watcher_status;

    BC_API watcher_status get_status();
    BC_API int get_unconfirmed_count();

    // - Thread implementation: --------

    /**
     * Tells the loop() method to return.
     */
    BC_API void stop();

    /**
     * Call this function from a separate thread. It will run for an
     * unlimited amount of time as it works to keep the transactions
     * in the watcher up-to-date with the network. The function will
     * eventually return when the watcher object is destroyed.
     */
    BC_API void loop();

    watcher(const watcher& copy) = delete;
    watcher& operator=(const watcher& copy) = delete;

    // Mainly used internally, but also appears in the example code:
    BC_API void watch_tx_mem(const hash_digest& txid);

    // Debugging code:
    BC_API void dump();

private:
    tx_db db_;
    zmq::context_t ctx_;

    // Cached addresses, for when we are disconnected:
    std::unordered_set<bc::payment_address> addresses_;
    bc::payment_address priority_address_;

    // Socket for talking to the thread:
    std::string socket_name_;
    zmq::socket_t socket_;

    // Methods for sending messages on that socket:
    void send_disconnect();
    void send_connect(std::string server);
    void send_watch_tx(hash_digest tx_hash);
    void send_watch_addr(payment_address address, unsigned poll_ms);
    void send_send(const transaction_type& tx);

    // The thread uses these callbacks, so put them in a mutex:
    std::mutex mutex_;
    callback cb_;
    block_height_callback height_cb_;
    tx_sent_callback tx_send_cb_;

    // Everything below this point is only touched by the thread:

    // Active connection (if any):
    struct connection
    {
        ~connection();
        connection(tx_db& db, void *ctx, tx_updater::send_handler&& on_send);

        bc::client::zeromq_socket socket;
        bc::client::obelisk_codec codec;
        tx_updater txu;
        address_updater adu;
    };
    connection* connection_;

    bool command(uint8_t* data, size_t size, zmq::socket_t& socket);
    void on_add(const transaction_type& tx);
    void on_height(size_t height);
    void on_sent(const std::error_code& error, const transaction_type& tx);
};

} // namespace libwallet

#endif

