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
#include <iterator>
#include <sstream>
#include <bitcoin/bitcoin.hpp>

namespace libwallet {

// Serialization stuff:
constexpr uint32_t serial_magic = 0x3eab61c3;
constexpr uint8_t id_address_row = 0x10;
constexpr uint8_t id_address_output = 0x11;
constexpr uint8_t id_tx_row = 0x20;
constexpr uint8_t id_get_tx_row = 0x30;

BC_API watcher::~watcher()
{
    shutdown_ = true;
    looper_.join();
}

BC_API watcher::watcher()
  : checked_priority_address_(false),
    shutdown_(false), request_done_(false), looper_([this](){loop();})
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

BC_API void watcher::send_tx(const transaction_type& tx)
{
    std::lock_guard<std::mutex> m(mutex_);
    send_tx_queue_.push_back(tx);
}

/**
 * Serializes the database for storage while the app is off.
 */
BC_API data_chunk watcher::serialize()
{
    std::lock_guard<std::mutex> m(mutex_);

    std::ostringstream stream;
    auto serial = make_serializer(std::ostreambuf_iterator<char>(stream));

    // Magic version bytes:
    serial.write_4_bytes(serial_magic);

    // Address table:
    for (const auto& row: addresses_)
    {
        serial.write_byte(id_address_row);
        // Address:
        payment_address address = row.first;
        serial.write_byte(address.version());
        serial.write_short_hash(address.hash());
        serial.write_8_bytes(row.second.last_height);
        // Output table:
        for (const auto& output: row.second.outputs)
        {
            serial.write_byte(id_address_output);
            serial.write_byte(address.version());
            serial.write_short_hash(address.hash());
            serial.write_hash(output.second.output.hash);
            serial.write_4_bytes(output.second.output.index);
            serial.write_8_bytes(output.second.value);
            serial.write_8_bytes(output.second.output_height);
            serial.write_hash(output.second.spend.hash);
            serial.write_4_bytes(output.second.spend.index);
        }
    }

    // Tx table:
    for (const auto& row: tx_table_)
    {
        serial.write_byte(id_tx_row);
        serial.write_hash(row.first);
        serial.set_iterator(satoshi_save(row.second, serial.iterator()));
    }

    // Pending tx list:
    for (auto row: get_tx_queue_)
    {
        serial.write_byte(id_get_tx_row);
        serial.write_hash(row.txid);
    }

    // OMG HAX!
    std::string str = stream.str();
    auto data = reinterpret_cast<const uint8_t*>(str.data());
    data_chunk out(data, data + str.size());
    return out;
}

template <class Serial>
static payment_address read_address(Serial& serial)
{
    auto version = serial.read_byte();
    auto hash = serial.read_short_hash();
    return payment_address(version, hash);
}

BC_API bool watcher::load(const data_chunk& data)
{
    auto serial = make_deserializer(data.begin(), data.end());
    std::unordered_map<payment_address, address_row> addresses;
    std::unordered_map<hash_digest, transaction_type> tx_table;
    std::deque<pending_get_tx> get_tx_queue;

    try
    {
        // Header bytes:
        if (serial_magic != serial.read_4_bytes())
            return false;

        while (serial.iterator() != data.end())
        {
            switch (serial.read_byte())
            {
                case id_address_row:
                {
                    auto address = read_address(serial);
                    address_row row;
                    row.last_height = serial.read_8_bytes();
                    addresses[address] = row;
                    break;
                }

                case id_address_output:
                {
                    auto address = read_address(serial);
                    txo_type row;
                    row.output.hash = serial.read_hash();
                    row.output.index = serial.read_4_bytes();
                    row.value = serial.read_8_bytes();
                    row.output_height = serial.read_8_bytes();
                    row.spend.hash = serial.read_hash();
                    row.spend.index = serial.read_4_bytes();

                    std::string id = utxo_to_id(row.output);
                    addresses[address].outputs[id] = row;
                    break;
                }

                case id_tx_row:
                {
                    auto hash = serial.read_hash();
                    transaction_type tx;
                    satoshi_load(serial.iterator(), data.end(), tx);
                    auto step = serial.iterator() + satoshi_raw_size(tx);
                    serial.set_iterator(step);
                    tx_table[hash] = tx;
                    break;
                }

                case id_get_tx_row:
                {
                    pending_get_tx row;
                    row.txid = serial.read_hash();
                    row.mempool = false;
                    break;
                }

                default:
                    return false;
            }
        }
    }
    catch (end_of_stream)
    {
        return false;
    }
    addresses_ = addresses;
    tx_table_ = tx_table;
    get_tx_queue_ = get_tx_queue;
    return true;
}

BC_API void watcher::watch_address(const payment_address& address)
{
    std::lock_guard<std::mutex> m(mutex_);
    addresses_[address] = address_row{0, std::unordered_map<std::string, txo_type>()};
}

/**
 * Checks a particular address more frequently (every other poll). To go back
 * to normal mode, pass an empty address.
 */
BC_API void watcher::prioritize_address(const payment_address& address)
{
    std::lock_guard<std::mutex> m(mutex_);
    priority_address_ = address;
}


BC_API transaction_type watcher::find_tx(hash_digest txid)
{
    auto tx = tx_table_.find(txid);
    if (tx != tx_table_.end())
        return tx->second;
    else
        return transaction_type();
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
 * Obtains a list of unspent outputs for an address. This is needed to spend
 * funds.
 */
BC_API output_info_list watcher::get_utxos(const payment_address& address)
{
    std::lock_guard<std::mutex> m(mutex_);
    output_info_list out;

    auto row = addresses_.find(address);
    if (row == addresses_.end())
        return out;

    for (auto& output: row->second.outputs) {
        // Skip outputs that are pending
        if (!output.second.output_height)
            continue;

        if (null_hash == output.second.spend.hash)
        {
            output_info_type info;
            info.point = output.second.output;
            info.value = output.second.value;
            out.push_back(info);
        }
    }
    return out;
}

/**
 * Places a transaction in the queue if we don't already have it in the db.
 * Assumes the mutex is already being held.
 */
void watcher::enqueue_tx_query(hash_digest txid, bool mempool)
{
    get_tx_queue_.push_back(pending_get_tx{txid, mempool});
}

/**
 * Inserts a tx into the database, assuming the address table has already been
 * updated.
 * Assumes the mutex is already being held.
 */
void watcher::insert_tx(const transaction_type& tx)
{
    tx_table_[hash_transaction(tx)] = tx;
    if (cb_)
        cb_(tx);
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
    std::vector<blockchain::history_row> history_sorted(history.begin(), history.end());
    std::sort(history_sorted.begin(), history_sorted.end(),
              [](const blockchain::history_row& a,
                 const blockchain::history_row& b) {
        return a.output_height < b.output_height;
    });

    // Update our state with the new info:
    size_t max_height = addresses_[address].last_height;
    size_t min_spend_height = (size_t) - 1;
    for (auto row: history_sorted)
    {
        enqueue_tx_query(row.output.hash);
        if (max_height <= row.output_height)
            max_height = row.output_height + 1;
        if (null_hash == row.spend.hash)
        {
            min_spend_height = std::min(min_spend_height, row.output_height);
        }
        txo_type output = {row.output, row.output_height, row.value, row.spend};

        std::string id = utxo_to_id(output.output);
        addresses_[address].outputs[id] = output;
    }
    // The last height is the earliest unspent output
    addresses_[address].last_height = std::min(min_spend_height, max_height);

    std::cout << "Got address " << address.encoded() << std::endl;
}

void watcher::got_tx(const std::error_code& ec, const transaction_type& tx)
{
    std::lock_guard<std::mutex> m(mutex_);
    request_done_ = true;

    if (ec)
    {
        std::cerr << "tx: Failed to fetch transaction: "
            << ec.message() << std::endl;
        return;
    }

    // Update our state with the new info:
    insert_tx(tx);
}

void watcher::got_tx_mem(const std::error_code& ec,
    const transaction_type& tx, hash_digest txid)
{
    std::lock_guard<std::mutex> m(mutex_);
    request_done_ = true;

    if (ec == error::not_found)
    {
        // Try the blockchain instead:
        enqueue_tx_query(txid, false);
        return;
    }
    else if (ec)
    {
        std::cerr << "tx: Failed to fetch transaction: "
            << ec.message() << std::endl;
        return;
    }

    // Update our state with the new info:
    insert_tx(tx);
}

void watcher::sent_tx(const std::error_code& ec)
{
    std::lock_guard<std::mutex> m(mutex_);
    request_done_ = true;

    if (ec)
    {
        std::cerr << "tx: Failed to send transaction" << std::endl;
        return;
    }
    // TODO: Fire callback? Update database?
    // The history update process will handle this for now.

    std::cout << "Tx sent"  << std::endl;
}

std::string watcher::utxo_to_id(output_point& pt)
{
    std::string id(encode_hex(pt.hash));
    id.append(std::to_string(pt.index));
    return id;
}

/**
 * Figures out the next thing for the query thread to work on. This happens
 * under a mutex lock.
 */
watcher::obelisk_query watcher::next_query()
{
    std::lock_guard<std::mutex> m(mutex_);
    obelisk_query out;

    // Process pending sends, if any:
    if (!send_tx_queue_.empty())
    {
        out.type = obelisk_query::send_tx;
        out.tx = send_tx_queue_.front();
        send_tx_queue_.pop_front();
        return out;
    }

    // Process pending tx queries, if any:
    if (!get_tx_queue_.empty())
    {
        auto pending = get_tx_queue_.front();
        get_tx_queue_.pop_front();
        if (tx_table_.end() == tx_table_.find(pending.txid))
        {
            out.type = obelisk_query::get_tx;
            if (pending.mempool)
                out.type = obelisk_query::get_tx_mem;
            out.txid = pending.txid;
            return out;
        }
    }

    // Handle the high-priority address, if we have one:
    if (!checked_priority_address_)
    {
        auto row = addresses_.find(priority_address_);
        if (row != addresses_.end())
        {
            out.type = obelisk_query::address_history;
            out.address = row->first;
            out.from_height = row->second.last_height;
            checked_priority_address_ = true;
            return out;
        }
    }
    checked_priority_address_ = false;

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
 * Connects to an obelisk server and makes a request. This happens outside
 * the mutex lock.
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
    usleep(100000); // OMG WTF!

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
                got_tx(ec, tx);
            };
            fullnode.blockchain.fetch_transaction(query.txid, handler);
            break;
        }
        case obelisk_query::get_tx_mem:
        {
            std::cout << "Get tx mem " << encode_hex(query.txid) << std::endl;

            auto txid = query.txid;
            auto handler = [this, txid](const std::error_code& ec,
                const transaction_type& tx)
            {
                got_tx_mem(ec, tx, txid);
            };
            fullnode.transaction_pool.fetch_transaction(query.txid, handler);
            break;
        }
        case obelisk_query::send_tx:
        {
            std::cout << "Send tx " << std::endl;

            auto handler = [this](const std::error_code& ec)
            {
                sent_tx(ec);
            };
            fullnode.protocol.broadcast_transaction(query.tx, handler);
            break;
        }
        default:
            break;
    }

    // Wait for results:
    int timeout = 0;
    usleep(100000); // OMG WTF!
    while (!request_done_ && timeout < 100)
    {
        fullnode.update();
        usleep(100000); // OMG WTF!
        timeout++;
    }
    pool.stop();
    pool.join();
    if (!request_done_)
    {
        std::cout << "Timed out" << std::endl;
    }
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

