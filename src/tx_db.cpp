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
#include <bitcoin/watcher/tx_db.hpp>

#include <iterator>
#include <iostream>

namespace libwallet {

// Serialization stuff:
constexpr uint32_t old_serial_magic = 0x3eab61c3; // From the watcher
constexpr uint32_t serial_magic = 0xfecdb760;
constexpr uint8_t serial_tx = 0x42;

BC_API tx_db::~tx_db()
{
}

BC_API tx_db::tx_db(add_handler&& on_add, height_handler&& on_height)
  : on_add_(std::move(on_add)),
    on_height_(std::move(on_height)),
    last_height_(0)
{
}

size_t tx_db::last_height()
{
    std::lock_guard<std::mutex> lock(mutex_);

    return last_height_;
}

bool tx_db::has_tx(bc::hash_digest tx_hash)
{
    std::lock_guard<std::mutex> lock(mutex_);

    return rows_.find(tx_hash) != rows_.end();
}

bc::transaction_type tx_db::get_tx(bc::hash_digest tx_hash)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto i = rows_.find(tx_hash);
    if (i == rows_.end())
        return bc::transaction_type();
    return i->second.tx;
}

size_t tx_db::get_tx_height(bc::hash_digest tx_hash)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto i = rows_.find(tx_hash);
    if (i == rows_.end())
        return 0;
    if (i->second.state != tx_state::confirmed)
        return 0;
    return i->second.block_height;
}

bc::output_info_list tx_db::get_utxos(const bc::payment_address& address)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // This is an O(n^2) algorithm!
    bc::output_info_list out;
    for (auto& row: rows_)
    {
        // TODO: Return all outputs, and let downstream filter confirmations
        if (row.second.state != tx_state::confirmed)
            continue;

        // Check each output:
        for (uint32_t i = 0; i < row.second.tx.outputs.size(); ++i)
        {
            auto& output = row.second.tx.outputs[i];
            bc::output_point point = {row.first, i};
            bc::payment_address to_address;
            if (bc::extract(to_address, output.script) &&
                address == to_address && is_unspent(point))
            {
                bc::output_info_type info = {point, output.value};
                out.push_back(info);
            }
        }
    }
    return out;
}

size_t tx_db::count_unconfirmed()
{
    std::lock_guard<std::mutex> lock(mutex_);

    size_t out = 0;
    for (auto row: rows_)
        if (row.second.state == tx_state::unconfirmed)
            ++out;
    return out;
}

void tx_db::send(const bc::transaction_type& tx)
{
    insert(tx, tx_state::unsent);
}

bc::data_chunk tx_db::serialize()
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::basic_ostringstream<uint8_t> stream;
    auto serial = bc::make_serializer(std::ostreambuf_iterator<uint8_t>(stream));

    // Magic version bytes:
    serial.write_4_bytes(serial_magic);

    // Last block height:
    serial.write_8_bytes(last_height_);

    // Tx table:
    for (const auto& row: rows_)
    {
        serial.write_byte(serial_tx);
        serial.write_hash(row.first);
        serial.set_iterator(satoshi_save(row.second.tx, serial.iterator()));
        serial.write_byte(static_cast<uint8_t>(row.second.state));
        serial.write_8_bytes(row.second.block_height);
        serial.write_byte(row.second.need_check);
    }

    // The copy is not very elegant:
    auto str = stream.str();
    return bc::data_chunk(str.begin(), str.end());
}

bool tx_db::load(const bc::data_chunk& data)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto serial = bc::make_deserializer(data.begin(), data.end());
    size_t last_height;
    std::map<bc::hash_digest, tx_row> rows;

    try
    {
        // Header bytes:
        auto magic = serial.read_4_bytes();
        if (old_serial_magic == magic)
            return true;
        if (serial_magic != magic)
            return false;

        // Last block height:
        last_height = serial.read_8_bytes();

        while (serial.iterator() != data.end())
        {
            if (serial.read_byte() != serial_tx)
                return false;

            bc::hash_digest hash = serial.read_hash();
            tx_row row;
            bc::satoshi_load(serial.iterator(), data.end(), row.tx);
            auto step = serial.iterator() + satoshi_raw_size(row.tx);
            serial.set_iterator(step);
            row.state = static_cast<tx_state>(serial.read_byte());
            row.block_height = serial.read_8_bytes();
            row.need_check = serial.read_byte();
            rows[hash] = std::move(row);
        }
    }
    catch (bc::end_of_stream)
    {
        return false;
    }
    last_height_ = last_height;
    rows_ = rows;
    return true;
}

void tx_db::dump()
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "height: " << last_height_ << std::endl;
    for (const auto& row: rows_)
    {
        std::cout << "================" << std::endl;
        std::cout << "hash: " << bc::encode_hex(row.first) << std::endl;
        std::string state;
        switch (row.second.state)
        {
        case tx_state::unsent:
            std::cout << "state: unsent" << std::endl;
            break;
        case tx_state::unconfirmed:
            std::cout << "state: unconfirmed" << std::endl;
            break;
        case tx_state::confirmed:
            std::cout << "state: confirmed" << std::endl;
            std::cout << "height: " << row.second.block_height << std::endl;
            if (row.second.need_check)
                std::cout << "needs check." << std::endl;
            break;
        }
    }
}

void tx_db::at_height(size_t height)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        last_height_ = height;

        // Check for blockchain forks:
        check_fork(height);
    }
    on_height_(last_height_);
}

bc::hash_digest tx_db::insert(const bc::transaction_type& tx, tx_state state)
{
    // Calculate the hash:
    bc::data_chunk data(satoshi_raw_size(tx));
    auto it = satoshi_save(tx, data.begin());
    BITCOIN_ASSERT(it == data.end());
    bc::hash_digest tx_hash = bc::bitcoin_hash(data);

    bool need_callback = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Do not stomp existing tx's:
        if (rows_.find(tx_hash) == rows_.end()) {
            rows_[tx_hash] = tx_row{tx, state, 0, false};
            need_callback = true;
        }
    }
    if (need_callback)
        on_add_(tx);

    return tx_hash;
}

void tx_db::confirmed(bc::hash_digest tx_hash, size_t block_height)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto i = rows_.find(tx_hash);
    BITCOIN_ASSERT(i != rows_.end());
    auto& row = i->second;

    // If the transaction was already confirmed in another block,
    // that means the chain has forked:
    if (row.state == tx_state::confirmed && row.block_height != block_height)
    {
        //on_fork_();
        check_fork(row.block_height);
    }

    row.state = tx_state::confirmed;
    row.block_height = block_height;
}

void tx_db::unconfirmed(bc::hash_digest tx_hash)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto i = rows_.find(tx_hash);
    if (i == rows_.end())
        return;
    auto row = i->second;

    if (row.state == tx_state::confirmed)
    {
        //on_fork_();
        check_fork(row.block_height);
    }

    row.need_check = true;
}

void tx_db::forget(bc::hash_digest tx_hash)
{
    std::lock_guard<std::mutex> lock(mutex_);

    rows_.erase(tx_hash);
}

void tx_db::foreach_unsent(hash_fn&& f)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto row: rows_)
        if (row.second.state == tx_state::unsent)
            f(row.first);
}

void tx_db::foreach_unconfirmed(hash_fn&& f)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto row: rows_)
        if (row.second.state == tx_state::unconfirmed)
            f(row.first);
}

void tx_db::foreach_forked(hash_fn&& f)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto row: rows_)
        if (row.second.state == tx_state::confirmed && row.second.need_check)
            f(row.first);
}

/**
 * It is possible that the blockchain has forked. Therefore, mark all
 * transactions just below the given height as needing to be checked.
 */
void tx_db::check_fork(size_t height)
{
    // Find the height of next-lower block that has transactions in it:
    size_t prev_height = 0;
    for (auto row: rows_)
        if (row.second.state == tx_state::confirmed &&
            row.second.block_height < height &&
            prev_height < row.second.block_height)
            prev_height = row.second.block_height;

    // Mark all transactions at that level as needing checked:
    for (auto row: rows_)
        if (row.second.state == tx_state::confirmed &&
            row.second.block_height == prev_height)
            row.second.need_check = true;
}

/**
 * Returns true if no other transaction in the database references this
 * output.
 */
bool tx_db::is_unspent(bc::output_point point)
{
    for (auto& row: rows_)
        for (auto& input: row.second.tx.inputs)
            if (point == input.previous_output)
                return false;
    return true;
}


} // libwallet

