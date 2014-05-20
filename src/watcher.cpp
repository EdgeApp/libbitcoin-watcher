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
#include <bitcoin/client/watcher.hpp>

#include <unistd.h>
#include <iostream>
#include <bitcoin/bitcoin.hpp>

namespace libwallet {

BC_API watcher::~watcher()
{
    shutdown_ = true;
    looper_.join();
}

BC_API watcher::watcher()
  : shutdown_(false), request_done_(false), looper_([this](){loop();})
{
}

BC_API void watcher::disconnect()
{
    std::lock_guard<std::mutex> m(mutex_);
    server_ = "";
}

BC_API void watcher::connect(const std::string& server)
{
    std::lock_guard<std::mutex> m(mutex_);
    server_ = server;
}

BC_API void watcher::watch_address(const payment_address& address)
{
    std::lock_guard<std::mutex> m(mutex_);
    addresses_[address] = address_row{0, std::vector<txo>()};
}

/**
 * Sets up the new-transaction callback. This callback will be called from
 * some random thread, so be sure to handle that with a mutex or such.
 */
BC_API void watcher::set_callback(callback& cb)
{
    std::lock_guard<std::mutex> m(mutex_);
    cb_ = cb;
}

/**
 * Places a transaction in the queue if we don't already have it in the db.
 * Assumes the mutex is already being held.
 */
void watcher::enqueue_tx_query(hash_digest hash)
{
    if (tx_table_.end() == tx_table_.find(hash))
        tx_query_queue_.push(hash);
}

void watcher::history_fetched(const std::error_code& ec,
    const payment_address& address, const blockchain::history_list& history)
{
    std::lock_guard<std::mutex> m(mutex_);
    request_done_ = true;

    if (ec)
    {
        std::cerr << "balance: Failed to fetch history: "
            << ec.message() << std::endl;
        return;
    }

    // Update our state with the new info:
    size_t max_height = addresses_[address].last_height;
    for (auto row: history)
    {
        enqueue_tx_query(row.output.hash);
        if (max_height <= row.output_height)
            max_height = row.output_height + 1;
        if (null_hash != row.spend.hash)
        {
            enqueue_tx_query(row.spend.hash);
            if (max_height <= row.spend_height)
                max_height = row.spend_height + 1;
        }
        txo output = {row.output, row.value, row.spend};
        addresses_[address].outputs.push_back(output);
    }
    addresses_[address].last_height = max_height;

    std::cout << "Got address " << address.encoded() << std::endl;
}

void watcher::tx_fetched(const std::error_code& ec,
    const transaction_type& tx)
{
    std::lock_guard<std::mutex> m(mutex_);
    request_done_ = true;

    if (ec)
    {
        std::cerr << "tx: Failed to fetch transaction: "
            << ec.message() << std::endl;
        return;
    }

    auto txid = hash_transaction(tx);

    // Update our state with the new info:
    tx_table_[txid] = tx;
    if (cb_)
        cb_(tx);

    std::cout << "Got tx " << encode_hex(txid) << std::endl;
}

/**
 * Figures out the next thing for the query thread to work on.
 */
watcher::obelisk_query watcher::next_query()
{
    std::lock_guard<std::mutex> m(mutex_);
    obelisk_query out;

    // Process pending tx queries, if any:
    if (!tx_query_queue_.empty())
    {
        out.type = obelisk_query::get_tx;
        out.txid = tx_query_queue_.front();
        tx_query_queue_.pop();
        return out;
    }

    // Stop if no addresses:
    if (!addresses_.size())
    {
        out.type = obelisk_query::none;
        return out;
    }

    // Advance the counter:
    ++last_address_;
    if (addresses_.size() <= last_address_)
        last_address_ = 0;

    // Find the indexed address:
    auto it = addresses_.begin();
    for (size_t i = 0; i < last_address_; ++i)
        ++it;
    out.type = obelisk_query::address_history;
    out.address = it->first;
    out.from_height = it->second.last_height;
    return out;
}

/**
 * Obtains the server string for the watcher thread, using a mutex lock.
 */
std::string watcher::get_server()
{
    std::lock_guard<std::mutex> m(mutex_);
    return server_;
}

/**
 * Connects to an obelisk server and makes a request.
 */
void watcher::do_query(const obelisk_query& query)
{
    // Connect to the sever:
    std::string server = get_server();
    if ("" == server)
    {
        std::cout << "No server" << std::endl;
        return;
    }
    threadpool pool(1);
    obelisk::fullnode_interface fullnode(pool, server);

    // Make the request:
    request_done_ = false;
    switch (query.type)
    {
        case obelisk_query::address_history:
        {
            std::cout << "Get address " << query.address.encoded() << std::endl;

            auto handler = [this, query](const std::error_code& ec,
                const blockchain::history_list& history)
            {
                history_fetched(ec, query.address, history);
            };
            fullnode.address.fetch_history(query.address,
                handler, query.from_height);
            break;
        }
        case obelisk_query::get_tx:
        {
            std::cout << "Get tx " << encode_hex(query.txid) << std::endl;

            auto handler = [this](const std::error_code& ec,
                const transaction_type& tx)
            {
                tx_fetched(ec, tx);
            };
            fullnode.blockchain.fetch_transaction(query.txid,
                handler);
            break;
        }
        default:
            break;
    }

    // Wait for results:
    while (!request_done_)
    {
        fullnode.update();
        usleep(100000); // OMG WTF!
    }
    pool.stop();
    pool.join();
}

/**
 * The main watcher loop. Sleeps for some amount of time, then wakes up
 * and checks an address (assuming we have server connection information).
 */
void watcher::loop()
{
    while (!shutdown_)
    {
        // Pick a random address to query:
        auto query = next_query();

        // Query the address:
        if (query.type != obelisk_query::none)
            do_query(query);
        else
            std::cout << "Skipping" << std::endl;
        sleep(5); // LOL!
    }
}

} // libwallet

