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
#ifndef LIBBITCOIN_PICKER_HPP
#define LIBBITCOIN_PICKER_HPP

#include <bitcoin/bitcoin.hpp>
#include <bitcoin/transaction.hpp>

namespace libwallet {

struct unspent_outputs_result
{
    bc::output_point_list points;
    uint64_t change;
};

struct unsigned_transaction_type
{
    bc::transaction_type tx;
    uint64_t fees;
};

struct fee_schedule
{
    uint64_t satoshi_per_kb;
};

struct tx_index
{
    size_t db_index;
    uint32_t tx_index;
};

class picker
{
public:
    BC_API picker();
    BC_API ~picker();

    BC_API void add_tx(bc::transaction_type& type);
    BC_API void watch_addr(std::string addr);
    BC_API bc::output_info_list unspent_outputs(std::string addr);

    BC_API bool create_unsigned_tx(unsigned_transaction_type& utx,
                            bc::payment_address fromAddr,
                            int64_t amountSatoshi,
                            bc::payment_address changeAddr,
                            fee_schedule& sched,
                            bc::transaction_output_list& outputs);
    BC_API bool sign_and_send(unsigned_transaction_type& type,
                            const bc::elliptic_curve_key& key);
private:
    void index_tx(bc::transaction_type& tx, size_t db_index);

    bool sign(unsigned_transaction_type& utx, const bc::elliptic_curve_key& key);
    bool send(bc::transaction_type& tx);

    bc::script_type build_pubkey_hash_script(const bc::short_hash& pubkey_hash);

    /* Database of all transactions */
    std::vector<bc::transaction_type> tx_database;

    /* Indexes into the tx_database of all transactions */
    std::map<std::string, std::vector<tx_index>> unspent_tx_index;

    /* Public Addresses to track unspent txs */
    std::set<std::string> watching;
};

}

#endif
