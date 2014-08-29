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
#ifndef LIBBITCOIN_WATCHER_TX_DB_HPP
#define LIBBITCOIN_WATCHER_TX_DB_HPP

#include <map>
#include <mutex>
#include <bitcoin/bitcoin.hpp>

namespace libwallet {

enum class tx_state
{
    /// The transaction has not been broadcast to the network.
    unsent,
    /// The network has seen this transaction, but not in a block.
    unconfirmed,
    /// The transaction is in a block.
    confirmed
};

/**
 * A list of transactions.
 *
 * This will eventually become a full database with queires mirroring what
 * is possible in the new libbitcoin-server protocol. For now, the goal is
 * to get something working.
 *
 * The fork-detection algorithm isn't perfect yet, since obelisk doesn't
 * provide the necessary information.
 */
class BC_API tx_db
{
public:
    typedef std::function<void (const bc::transaction_type& tx)> add_handler;
    typedef std::function<void (const size_t)> height_handler;

    BC_API ~tx_db();
    BC_API tx_db(add_handler&& on_add, height_handler&& on_height);

    /**
     * Returns the highest block that this database has seen.
     */
    BC_API size_t last_height();

    /**
     * Returns true if the database contains a transaction.
     */
    BC_API bool has_tx(bc::hash_digest tx_hash);

    /**
     * Obtains a transaction from the database.
     */
    BC_API bc::transaction_type get_tx(bc::hash_digest tx_hash);

    /**
     * Finds a transaction's height, or 0 if it isn't in a block.
     */
    BC_API size_t get_tx_height(bc::hash_digest tx_hash);

    /**
     * Get the unspent outputs corresponding to an address.
     */
    BC_API bc::output_info_list get_utxos(const bc::payment_address& address);

    /**
     * Get all unspent outputs in the database.
     */
    BC_API bc::output_info_list get_utxos();

    /**
     * Returns the number of unconfirmed transactions in the database.
     */
    BC_API size_t count_unconfirmed();

    /**
     * Adds an unsent transaction to the database.
     */
    BC_API void send(const bc::transaction_type& tx);

    /**
     * Write the database to an in-memory blob.
     */
    BC_API bc::data_chunk serialize();

    /**
     * Reconstitute the database from an in-memory blob.
     */
    BC_API bool load(const bc::data_chunk& data);

    /**
     * Debug dump to show db contents.
     */
    BC_API void dump();

private:
    // - Updater: ----------------------
    friend class tx_updater;

    /**
     * Computes a transaction's hash.
     */
    static bc::hash_digest hash_tx(const bc::transaction_type &tx);

    /**
     * Updates the block height.
     */
    BC_API void at_height(size_t height);

    /**
     * Insert a new transaction into the database.
     * @param seen True if the transaction is in the bitcoin network,
     * or false if it comes from the app and hasn't been sent yet.
     */
    BC_API bc::hash_digest insert(const bc::transaction_type &tx, tx_state state);

    /**
     * Mark a transaction as confirmed.
     * TODO: Require the block hash as well, once obelisk provides this.
     */
    BC_API void confirmed(bc::hash_digest tx_hash, size_t block_height);

    /**
     * Mark a transaction as unconfirmed.
     */
    BC_API void unconfirmed(bc::hash_digest tx_hash);

    /**
     * Delete a transaction.
     * This can happen when the network rejects a spend request.
     */
    BC_API void forget(bc::hash_digest tx_hash);

    typedef std::function<void (bc::hash_digest tx_hash)> hash_fn;
    BC_API void foreach_unconfirmed(hash_fn&& f);
    BC_API void foreach_forked(hash_fn&& f);

    typedef std::function<void (const bc::transaction_type& tx)> tx_fn;
    BC_API void foreach_unsent(tx_fn&& f);

    // - Internal: ---------------------
    void check_fork(size_t height);
    bool is_unspent(bc::output_point point);

    // Guards access to object state:
    std::mutex mutex_;
    add_handler on_add_;
    height_handler on_height_;

    // The last block seen on the network:
    size_t last_height_;

    /**
     * A single row in the transaction database.
     */
    struct tx_row
    {
        // The transaction itself:
        bc::transaction_type tx;

        // State machine:
        tx_state state;
        size_t block_height;
        //bc::hash_digest block_hash; // TODO: Fix obelisk to return this

        // The transaction is certainly in a block, but there is some
        // question whether or not that block is on the main chain:
        bool need_check;
    };
    std::map<bc::hash_digest, tx_row> rows_;
};

} // namespace libwallet

#endif

