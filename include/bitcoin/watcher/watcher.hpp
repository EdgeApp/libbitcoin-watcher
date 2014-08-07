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

#include <atomic>
#include <functional>
#include <deque>
#include <unordered_map>
#include <bitcoin/client.hpp>
#include <zmq.hpp>

namespace libwallet {

using namespace libbitcoin;

/**
 * Maintains a connection to an obelisk servers, and uses that connection to
 * watch one or more bitcoin addresses for activity (Actually, since the
 * client library is broken, it opens a new obelisk connection every time.)
 */
class watcher
  : public libbitcoin::client::sleeper
{
public:
    BC_API ~watcher();
    BC_API watcher(void *ctx);

    /**
     * This must be called before doing anything else. If something goes
     * wrong, it returns false and the watcher will be non-functional.
     */
    BC_API bool connect(const std::string& server);

    /**
     * This is where all the work happens, including processing incoming
     * messages and performing periodic queries. Call this method whenever
     * the socket has incoming messages, or after the returned timeout has
     * elapsed.
     */
    BC_API virtual std::chrono::milliseconds wakeup();

    /**
     * Returns a zeromq poll item that can be used to determine if the
     * watcher needs its `wakeup` method called.
     */
    BC_API zmq_pollitem_t pollitem();

    BC_API void send_tx(const transaction_type& tx);
    BC_API void request_height();

    BC_API data_chunk serialize();
    BC_API bool load(const data_chunk& data);

    BC_API void watch_address(const payment_address& address);
    BC_API void watch_tx_mem(const hash_digest& txid);
    BC_API void prioritize_address(const payment_address& address);
    BC_API transaction_type find_tx(hash_digest txid);

    typedef std::function<void (const transaction_type&)> callback;
    BC_API void set_callback(callback& cb);

    typedef std::function<void (const size_t)> block_height_callback;
    BC_API void set_height_callback(block_height_callback& cb);

    BC_API output_info_list get_utxos(const payment_address& address);
    BC_API size_t get_last_block_height();
    BC_API bool get_tx_height(hash_digest txid, int& height);

    typedef enum {
        watcher_sync_ok = 0,
        watcher_syncing
    } watcher_status;

    BC_API watcher_status get_status();
    BC_API int get_unconfirmed_count();

    watcher(const watcher& copy) = delete;
    watcher& operator=(const watcher& copy) = delete;

private:
    libbitcoin::client::zeromq_socket socket_;
    libbitcoin::client::obelisk_codec codec_;

    /**
     * Last block height...duh
     */
    size_t last_block_height_;

    /**
     * A transaction output putting funds into an address. If the spend
     * field is null, this output is unspent.
     */
    struct txo_type {
        output_point output;
        size_t output_height;
        uint64_t value;
        input_point spend; // null if this output hasn't been spent
    };

    /**
     * An entry in the address table.
     */
    struct address_row {
        size_t last_height;
        bool stale; // have we fetched this address yet?
        std::unordered_map<std::string, txo_type> outputs;
    };

    // Addresses we care about:
    std::unordered_map<payment_address, address_row> addresses_;

    // Transaction table:
    struct tx_row {
        transaction_type tx;
        size_t output_height;
        bool relevant;
    };
    std::unordered_map<hash_digest, tx_row> tx_table_;

    /**
     * If mem pool tx outputs, mark those outputs as pending so they are
     * excluded from utxos
     */
    std::unordered_map<std::string, bool> output_pending_;
    /**
     * Check mem pool for these transactions, they are removed once they are in
     * the blockchain
     */
    std::unordered_map<hash_digest, size_t> watch_txs_;

    // Periodic address checking:
    size_t last_address_;
    payment_address priority_address_;
    bool checked_priority_address_;
    std::chrono::steady_clock::time_point last_check_;

    // Transaction callback:
    callback cb_;
    block_height_callback height_cb_;

    // Database update (the mutex must be held before calling):
    void get_history(payment_address address, size_t from_height);
    void enqueue_tx_query(hash_digest txid, hash_digest parent_txid, bool mempool=true);
    void insert_tx(const transaction_type& tx, const hash_digest parent_txid);
    void enque_all_inputs(const transaction_type& tx);
    bool has_all_prev_outputs(const hash_digest& txid);
    void mark_outputs_pending(const transaction_type& tx, bool pending);

    // Callbacks (these grab the mutex):
    void height_fetch_error(const std::error_code& ec);
    void history_fetch_error(const std::error_code& ec);
    void get_tx_error(const std::error_code& ec);
    void get_tx_mem_error(const std::error_code& ec,
        hash_digest txid, hash_digest parent_txid);
    void send_tx_error(const std::error_code& ec);

    void height_fetched(size_t height);
    void history_fetched(const payment_address& address,
        const blockchain::history_list& history);
    void got_tx(const transaction_type& tx, const hash_digest& parent_txid);
    void sent_tx(const transaction_type& tx);

    std::string utxo_to_id(output_point& pt);

    bool next_query();
};

} // namespace libwallet

#endif

