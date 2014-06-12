/*
 * Copyright (c) 2011-2014 libwallet developers (see AUTHORS)
 *
 * This file is part of libbitcoin-client.
 *
 * libbitcoin-client is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
:*
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
    std::lock_guard<std::recursive_mutex> m(mutex_);
    server_ = "";
}

BC_API void watcher::connect(const std::string& server)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
    server_ = server;
}

BC_API void watcher::send_tx(const transaction_type& tx)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
    send_tx_queue_.push_back(tx);
}

/**
 * Serializes the database for storage while the app is off.
 */
BC_API data_chunk watcher::serialize()
{
    std::lock_guard<std::recursive_mutex> m(mutex_);

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
        serial.set_iterator(satoshi_save(row.second.tx, serial.iterator()));
        serial.write_8_bytes(row.second.output_height);
        serial.write_byte(row.second.relevant);
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
    std::unordered_map<hash_digest, tx_row> tx_table;
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
                    size_t height = serial.read_8_bytes();
                    bool relevant = serial.read_byte();
                    tx_table[hash] = tx_row{tx, height, relevant};
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
    std::lock_guard<std::recursive_mutex> m(mutex_);
    addresses_[address] = address_row{0, std::unordered_map<std::string, txo_type>()};
}

/**
 * Checks a particular address more frequently (every other poll). To go back
 * to normal mode, pass an empty address.
 */
BC_API void watcher::prioritize_address(const payment_address& address)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
    priority_address_ = address;
}


BC_API transaction_type watcher::find_tx(hash_digest txid)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
    auto tx = tx_table_.find(txid);
    if (tx != tx_table_.end())
        return tx->second.tx;
    else
        return transaction_type();
}

/**
 * Sets up the new-transaction callback. This callback will be called from
 * some random thread, so be sure to handle that with a mutex or such.
 */
BC_API void watcher::set_callback(callback& cb)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
    cb_ = cb;
}

/**
 * Obtains a list of unspent outputs for an address. This is needed to spend
 * funds.
 */
BC_API output_info_list watcher::get_utxos(const payment_address& address)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
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

BC_API size_t watcher::get_last_block_height()
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
    return last_block_height_;
}

BC_API size_t watcher::get_tx_height(hash_digest txid)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
    auto row = tx_table_.find(txid);
    if (row == tx_table_.end())
        return 0;
    else
        return row->second.output_height;
}

/**
 * Places a transaction in the queue if we don't already have it in the db.
 * Assumes the mutex is already being held.
 */
void watcher::enqueue_tx_query(hash_digest txid, hash_digest parent_txid, bool mempool)
{
    if (mempool)
        get_tx_queue_.push_back(pending_get_tx{txid, parent_txid, mempool});
    else
        get_tx_queue_.push_front(pending_get_tx{txid, parent_txid, mempool});
}

/**
 * Inserts a tx into the database, assuming the address table has already been
 * updated.
 * Assumes the mutex is already being held.
 */
void watcher::insert_tx(const transaction_type& tx, const hash_digest parent_txid)
{
    hash_digest txid = hash_transaction(tx);
    tx_table_[txid] = tx_row{tx, 0, parent_txid == null_hash};

    if (cb_)
    {
        if (has_all_prev_outputs(txid))
        {
            std::cout << "Calling cb with tx" << std::endl;
            std::cout << pretty(tx) << std::endl;
            cb_(tx);
        }
        else if (has_all_prev_outputs(parent_txid))
        {
            std::cout << "Calling cb for parent" << std::endl;
            std::cout << pretty(tx_table_[parent_txid].tx) << std::endl;
            cb_(tx_table_[parent_txid].tx);
        }
        else if (parent_txid == null_hash)
        {
            enque_all_inputs(tx);
        }
    }
}

/**
 * Enque all previous txs for this transaction
 * Assumes the mutex is already being held.
 */
void watcher::enque_all_inputs(const transaction_type& tx)
{
    for (auto& input : tx.inputs)
    {
        enqueue_tx_query(input.previous_output.hash, hash_transaction(tx));
    }
}

/**
 * Fetches transactions of the tx's inputs
 *
 * Assumes the mutex is already being held.
 */
bool watcher::has_all_prev_outputs(const hash_digest& txid)
{
    if (txid == null_hash)
        return false;

    auto row = tx_table_.find(txid);
    if (row == tx_table_.end())
        return false;

    for (auto& input : row->second.tx.inputs)
    {
        auto& prev = input.previous_output;
        if (tx_table_.find(prev.hash) == tx_table_.end())
            return false;
    }
    return true;
}

void watcher::height_fetched(const std::error_code& ec, size_t height)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
    request_done_ = true;

    if (ec)
    {
        std::cerr << "balance: Failed to fetch last block height: "
            << ec.message() << std::endl;
        return;
    }
    last_block_height_ = height;

    std::cout << "Last Height " << height << std::endl;
}

void watcher::history_fetched(const std::error_code& ec,
    const payment_address& address, const blockchain::history_list& history)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
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
    for (auto row: history_sorted)
    {
        enqueue_tx_query(row.output.hash, null_hash);
        auto tx = tx_table_.find(row.output.hash);
        if (tx != tx_table_.end())
            tx->second.output_height = row.output_height;

        if (row.spend.hash != null_hash)
        {
            enqueue_tx_query(row.spend.hash);
            auto tx = tx_table_.find(row.spend.hash);
            if (tx != tx_table_.end())
                tx->second.output_height = row.spend_height;
        }


        txo_type output = {row.output, row.output_height, row.value, row.spend};
        std::string id = utxo_to_id(output.output);
        addresses_[address].outputs[id] = output;
    }
    addresses_[address].last_height = 0;

    std::cout << "Got address " << address.encoded() << std::endl;
}

void watcher::got_tx(const std::error_code& ec,
                     const transaction_type& tx,
                     hash_digest parent_txid)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
    request_done_ = true;

    if (ec)
    {
        std::cerr << "tx: Failed to fetch transaction: "
            << ec.message() << std::endl;
        return;
    }

    // Update our state with the new info:
    insert_tx(tx, parent_txid);
}

void watcher::got_tx_mem(const std::error_code& ec,
    const transaction_type& tx, hash_digest txid, hash_digest parent_txid)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
    request_done_ = true;

    if (ec == error::not_found)
    {
        // Try the blockchain instead:
        enqueue_tx_query(txid, parent_txid, false);
        return;
    }
    else if (ec)
    {
        std::cerr << "tx: Failed to fetch transaction: "
            << ec.message() << std::endl;
        return;
    }

    // Update our state with the new info:
    insert_tx(tx, parent_txid);
}

void watcher::sent_tx(const std::error_code& ec)
{
    std::lock_guard<std::recursive_mutex> m(mutex_);
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
    std::lock_guard<std::recursive_mutex> m(mutex_);
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
        auto tx_row = tx_table_.find(pending.txid);
        if (tx_row == tx_table_.end())
        {
            out.type = obelisk_query::get_tx;
            if (pending.mempool)
                out.type = obelisk_query::get_tx_mem;
            out.txid = pending.txid;
            out.parent_txid = pending.parent_txid;
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

    if (check_height)
    {
        check_height = false;
        out.type = obelisk_query::block_height;
        return out;
    }
    // Advance the counter:
    ++last_address_;
    if (addresses_.size() <= last_address_)
    {
        last_address_ = 0;
        check_height = true;
    }

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
    std::lock_guard<std::recursive_mutex> m(mutex_);
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
        case obelisk_query::block_height:
        {
            std::cout << "Fetch block height " << std::endl;

            auto handler = [this](const std::error_code& ec, size_t height)
            {
                height_fetched(ec, height);
            };
            fullnode.blockchain.fetch_last_height(handler);
            break;
        }
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

            auto handler = [this, query](const std::error_code& ec,
                const transaction_type& tx)
            {
                got_tx(ec, tx, query.parent_txid);
            };
            fullnode.blockchain.fetch_transaction(query.txid, handler);
            break;
        }
        case obelisk_query::get_tx_mem:
        {
            std::cout << "Get tx mem " << encode_hex(query.txid) << std::endl;

            auto txid = query.txid;
            auto handler = [this, txid, query](const std::error_code& ec,
                const transaction_type& tx)
            {
                got_tx_mem(ec, tx, txid, query.parent_txid);
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
        if (query.type == obelisk_query::get_tx
                || query.type == obelisk_query::get_tx_mem)
            pool_sleep = 0;
        else
            pool_sleep = 5;

        sleep(pool_sleep); // LOL!
    }
}

} // libwallet

