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
#ifndef LIBBITCOIN_WATCHER_TX_UPDATER_HPP
#define LIBBITCOIN_WATCHER_TX_UPDATER_HPP

#include <bitcoin/watcher/tx_db.hpp>
#include <bitcoin/client.hpp>
#include <bitcoin/bitcoin.hpp>

namespace libwallet {

/**
 * Interface containing the events the updater can trigger.
 */
class BC_API tx_callbacks
{
public:
    virtual ~tx_callbacks() {};

    /**
     * Called when the updater inserts a transaction into the database.
     */
    virtual void on_add(const bc::transaction_type& tx) = 0;

    /**
     * Called when the updater detects a new block.
     */
    virtual void on_height(size_t height) = 0;

    /**
     * Called when the updater has validated a transaction for send.
     */
    virtual void on_send(const std::error_code& error,
        const bc::transaction_type& tx) = 0;

    /**
     * Called when the updater sees an unexpected obelisk server failure.
     */
    virtual void on_fail(const std::error_code& error) = 0;
};

/**
 * Syncs a set of transactions with the bitcoin server.
 */
class BC_API tx_updater
  : public bc::client::sleeper
{
public:
    BC_API ~tx_updater();
    BC_API tx_updater(tx_db& db, bc::client::obelisk_codec& codec,
        tx_callbacks& callbacks);
    void start();

    BC_API void watch(bc::hash_digest tx_hash);
    BC_API void send(bc::transaction_type tx);

    // Sleeper interface:
    virtual std::chrono::milliseconds wakeup();

    // Hack:
    void fail(const std::error_code& error)
    {
        callbacks_.on_fail(error);
    }

private:
    void queue_get_indices();

    // Server queries:
    void get_height();
    void get_tx(bc::hash_digest tx_hash);
    void get_tx_mem(bc::hash_digest tx_hash);
    void get_index(bc::hash_digest tx_hash);
    void send_tx(const bc::transaction_type& tx);

    tx_db& db_;
    bc::client::obelisk_codec& codec_;
    tx_callbacks& callbacks_;

    size_t queued_get_indices_;
    std::chrono::steady_clock::time_point last_wakeup_;
};

} // namespace libwallet

#endif

