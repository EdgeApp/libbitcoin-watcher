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
#include <bitcoin/watcher/watcher.hpp>

#include <unistd.h>
#include <iostream>
#include <iterator>
#include <sstream>

namespace libwallet {

using std::placeholders::_1;
using std::placeholders::_2;

constexpr unsigned default_poll = 10000;
constexpr unsigned priority_poll = 1000;

static unsigned watcher_id = 0;

enum {
    msg_quit,
    msg_disconnect,
    msg_connect,
    msg_watch_tx,
    msg_watch_addr,
    msg_send
};

BC_API watcher::~watcher()
{
}

BC_API watcher::watcher()
  : db_(std::bind(&watcher::on_add, this, _1),
        std::bind(&watcher::on_height, this, _1)),
    socket_(ctx_, ZMQ_PAIR),
    connection_(nullptr)
{
    std::stringstream name;
    name << "inproc://watcher-" << watcher_id++;
    socket_name_ = name.str();
    socket_.bind(socket_name_.c_str());
    int linger = 0;
    socket_.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
}

BC_API void watcher::disconnect()
{
    send_disconnect();
}

static bool is_valid(const payment_address& address)
{
    return address.version() != payment_address::invalid_version;
}

BC_API void watcher::connect(const std::string& server)
{
    send_connect(server);
    for (auto& address: addresses_)
        send_watch_addr(address.first, address.second);
    if (is_valid(priority_address_))
        send_watch_addr(priority_address_, priority_poll);
}

BC_API void watcher::send_tx(const transaction_type& tx)
{
    send_send(tx);
}

/**
 * Serializes the database for storage while the app is off.
 */
BC_API data_chunk watcher::serialize()
{
    return db_.serialize();
}

BC_API bool watcher::load(const data_chunk& data)
{
    return db_.load(data);
}

BC_API void watcher::watch_address(const payment_address& address, unsigned poll_ms)
{
    addresses_[address] = poll_ms;
    send_watch_addr(address, poll_ms);
}

BC_API void watcher::watch_tx_mem(const hash_digest& txid)
{
    send_watch_tx(txid);
    std::cout << "Watching tx " << txid << std::endl;
}

/**
 * Checks a particular address more frequently (every other poll). To go back
 * to normal mode, pass an empty address.
 */
BC_API void watcher::prioritize_address(const payment_address& address)
{
    if (is_valid(priority_address_))
        send_watch_addr(priority_address_, default_poll);
    priority_address_ = address;
    if (is_valid(priority_address_))
        send_watch_addr(priority_address_, priority_poll);
}

BC_API transaction_type watcher::find_tx(hash_digest txid)
{
    return db_.get_tx(txid);
}

/**
 * Sets up the new-transaction callback. This callback will be called from
 * some random thread, so be sure to handle that with a mutex or such.
 */
BC_API void watcher::set_callback(callback&& cb)
{
    std::lock_guard<std::mutex> lock(cb_mutex_);
    cb_ = std::move(cb);
}

/**
 * Sets up the change in block heightcallback.
 */
BC_API void watcher::set_height_callback(block_height_callback&& cb)
{
    std::lock_guard<std::mutex> lock(cb_mutex_);
    height_cb_ = std::move(cb);
}

/**
 * Sets up the tx sent callback
 */
BC_API void watcher::set_tx_sent_callback(tx_sent_callback&& cb)
{
    std::lock_guard<std::mutex> lock(cb_mutex_);
    tx_send_cb_ = std::move(cb);
}

/**
 * Obtains a list of unspent outputs for an address. This is needed to spend
 * funds.
 */
BC_API output_info_list watcher::get_utxos(const payment_address& address)
{
    return db_.get_utxos(address);
}

BC_API output_info_list watcher::get_utxos()
{
    return db_.get_utxos();
}

BC_API size_t watcher::get_last_block_height()
{
    return db_.last_height();
}

BC_API bool watcher::get_tx_height(hash_digest txid, int& height)
{
    height = db_.get_tx_height(txid);
    return db_.has_tx(txid);
}

BC_API watcher::watcher_status watcher::get_status()
{
    // This is a terrible hack!
    return watcher_sync_ok;
}

BC_API int watcher::get_unconfirmed_count()
{
    return db_.count_unconfirmed();
}

void watcher::dump(std::ostream& out)
{
    db_.dump(out);
}

BC_API void watcher::stop()
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    uint8_t req = msg_quit;
    socket_.send(&req, 1);
}

BC_API void watcher::loop()
{
    zmq::socket_t socket(ctx_, ZMQ_PAIR);
    socket.connect(socket_name_.c_str());
    int linger = 0;
    socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

    bool done = false;
    while (!done)
    {
        int delay = -1;
        std::vector<zmq_pollitem_t> items;
        items.reserve(2);
        zmq_pollitem_t inproc_item = { socket, 0, ZMQ_POLLIN, 0 };
        items.push_back(inproc_item);
        if (connection_)
        {
            items.push_back(connection_->socket.pollitem());
            auto next_wakeup = connection_->codec.wakeup();
            next_wakeup = bc::client::min_sleep(next_wakeup,
                connection_->txu.wakeup());
            next_wakeup = bc::client::min_sleep(next_wakeup,
                connection_->adu.wakeup());
            if (next_wakeup.count())
                delay = next_wakeup.count();
        }
        zmq::poll(items.data(), items.size(), delay);

        if (connection_ && items[1].revents)
            connection_->socket.forward(connection_->codec);
        if (items[0].revents)
        {
            zmq::message_t msg;
            socket.recv(&msg);
            if (!command(static_cast<uint8_t*>(msg.data()), msg.size()))
                done = true;
        }
    }
    delete connection_;
}

void watcher::send_disconnect()
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    uint8_t req = msg_disconnect;
    socket_.send(&req, 1);
}

void watcher::send_connect(std::string server)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    std::basic_ostringstream<uint8_t> stream;
    auto serial = bc::make_serializer(std::ostreambuf_iterator<uint8_t>(stream));
    serial.write_byte(msg_connect);
    serial.write_data(server);
    auto str = stream.str();
    socket_.send(str.data(), str.size());
}

void watcher::send_watch_tx(hash_digest tx_hash)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    std::basic_ostringstream<uint8_t> stream;
    auto serial = bc::make_serializer(std::ostreambuf_iterator<uint8_t>(stream));
    serial.write_byte(msg_watch_tx);
    serial.write_hash(tx_hash);
    auto str = stream.str();
    socket_.send(str.data(), str.size());
}

void watcher::send_watch_addr(payment_address address, unsigned poll_ms)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    std::basic_ostringstream<uint8_t> stream;
    auto serial = bc::make_serializer(std::ostreambuf_iterator<uint8_t>(stream));
    serial.write_byte(msg_watch_addr);
    serial.write_byte(address.version());
    serial.write_short_hash(address.hash());
    serial.write_4_bytes(poll_ms);
    auto str = stream.str();
    socket_.send(str.data(), str.size());
}

void watcher::send_send(const transaction_type& tx)
{
    std::lock_guard<std::mutex> lock(socket_mutex_);

    std::basic_ostringstream<uint8_t> stream;
    auto serial = bc::make_serializer(std::ostreambuf_iterator<uint8_t>(stream));
    serial.write_byte(msg_send);
    serial.set_iterator(satoshi_save(tx, serial.iterator()));
    auto str = stream.str();
    socket_.send(str.data(), str.size());
}

bool watcher::command(uint8_t* data, size_t size)
{
    auto serial = bc::make_deserializer(data, data + size);
    switch (serial.read_byte())
    {
    default:
    case msg_quit:
        delete connection_;
        connection_ = nullptr;
        return false;

    case msg_disconnect:
        delete connection_;
        connection_ = nullptr;
        return true;

    case msg_connect:
        {
            std::string server(data + 1, data + size);
            delete connection_;
            connection_ = new connection(db_, ctx_,
                std::bind(&watcher::on_sent, this, _1, _2));
            if (!connection_->socket.connect(server))
            {
                delete connection_;
                connection_ = nullptr;
                return true;
            }
            connection_->txu.start();
        }
        return true;

    case msg_watch_tx:
        {
            auto tx_hash = serial.read_hash();
            if (connection_)
                connection_->txu.watch(tx_hash);
        }
        return true;

    case msg_watch_addr:
        {
            auto version = serial.read_byte();
            auto hash = serial.read_short_hash();
            payment_address address(version, hash);
            std::chrono::milliseconds poll_time(serial.read_4_bytes());
            if (connection_)
                connection_->adu.watch(address, poll_time);
        }
        return true;

    case msg_send:
        {
            transaction_type tx;
            bc::satoshi_load(serial.iterator(), data + size, tx);
            if (connection_)
                connection_->txu.send(tx);
        }
        return true;
    }
}

void watcher::on_add(const transaction_type& tx)
{
    std::lock_guard<std::mutex> lock(cb_mutex_);
    if (cb_)
        cb_(tx);
}

void watcher::on_height(size_t height)
{
    std::lock_guard<std::mutex> lock(cb_mutex_);
    if (height_cb_)
        height_cb_(height);
}

void watcher::on_sent(const std::error_code& error, const transaction_type& tx)
{
    std::lock_guard<std::mutex> lock(cb_mutex_);
    if (tx_send_cb_)
        tx_send_cb_(error, tx);
}

watcher::connection::~connection()
{
}

watcher::connection::connection(tx_db& db, void *ctx, tx_updater::send_handler&& on_send)
  : socket(ctx),
    codec(socket),
    txu(db, codec, std::move(on_send)),
    adu(txu, codec)
{
}

} // libwallet

