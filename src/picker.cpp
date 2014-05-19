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
#include <bitcoin/picker.hpp>

#include <unistd.h>
#include <iostream>
#include <bitcoin/bitcoin.hpp>
#include <wallet/transaction.hpp>

namespace libwallet {

BC_API picker::picker()
{
}

BC_API picker::~picker()
{
}

BC_API void picker::add_tx(bc::transaction_type& tx)
{
    tx_database.push_back(tx);
    index_tx(tx, tx_database.size() - 1);
}

BC_API void picker::index_tx(bc::transaction_type& tx, size_t db_index)
{
    bc::transaction_output_list os = tx.outputs;
    bc::transaction_output_list::iterator it = os.begin();
    for (size_t oidx = 0; it != os.end(); ++it, ++oidx)
    {
        bc::payment_address addr;
        if (!bc::extract(addr, it->script))
            continue;

        /* Are we watching this address? */
        if (watching.find(addr.encoded()) != watching.end())
        {
            tx_index t = tx_index();
            t.db_index = db_index;
            t.tx_index = oidx;

            /* Append information to address index */
            std::vector<tx_index> addr_indexes;
            auto it = unspent_tx_index.find(addr.encoded());
            if (it != unspent_tx_index.end())
            {
                it->second.push_back(t);
            }
        }
    }
}

BC_API void picker::watch_addr(std::string addr)
{
    watching.insert(addr);
    unspent_tx_index.insert(
        std::pair<std::string, std::vector<tx_index>>(addr, std::vector<tx_index>()));
}

BC_API bc::output_info_list picker::unspent_outputs(std::string addr)
{
    bc::output_info_list unspent;
    auto it = unspent_tx_index.find(addr);
    if (it == unspent_tx_index.end())
        return unspent;

    std::vector<tx_index> indexes = it->second;
    std::vector<tx_index>::iterator vit = indexes.begin();
    for ( ; vit != indexes.end(); ++vit)
    {
        bc::transaction_type t = tx_database.at(vit->db_index);
        bc::transaction_output_type ot = t.outputs.at(vit->tx_index);

        bc::payment_address paddr;
        if (bc::extract(paddr, ot.script))
        {
            bc::hash_digest hash = bc::hash_transaction(t);
            if (hash == bc::null_hash)
                break;

            bc::output_info_type out;
            out.value = ot.value;
            out.point.hash = hash;
            out.point.index = vit->tx_index;
            unspent.push_back(out);
        }
    }
    return unspent;
}

BC_API bool picker::create_unsigned_tx(unsigned_transaction_type& utx,
                                       bc::payment_address fromAddr,
                                       int64_t amountSatoshi,
                                       bc::payment_address changeAddr,
                                       fee_schedule& sched,
                                       bc::transaction_output_list& outputs)
{
    bc::output_info_list unspent = unspent_outputs(fromAddr.encoded());
    libwallet::select_outputs_result os = libwallet::select_outputs(unspent, amountSatoshi);
    /* Do we have the funds ? */
    if (os.points.size() <= 0)
        return false;

    utx.tx.version = 1;
    utx.tx.locktime = 0;
    bc::output_point_list::iterator it = os.points.begin();
    for (; it != os.points.end(); it++)
    {
        bc::transaction_input_type input;
        input.previous_output.index = it->index;
        input.previous_output.hash = it->hash;
        utx.tx.inputs.push_back(input);
    }
    utx.tx.outputs = outputs;
    /* If change is needed, add the change address */
    if (os.change > 0) {
        bc::transaction_output_type change;
        change.value = os.change;
        change.script = build_pubkey_hash_script(changeAddr.hash());
        utx.tx.outputs.push_back(change);
    }
    /* Calculate fees with this transaction */
    utx.fees = sched.satoshi_per_kb * bc::satoshi_raw_size(utx.tx);
    return true;
}

BC_API bool picker::sign_and_send(unsigned_transaction_type& utx,
                                   const bc::elliptic_curve_key& key)
{
    if (!sign(utx, key))
    {
        return false;
    }
    return send(utx.tx);
}

bool picker::sign(unsigned_transaction_type& utx, const bc::elliptic_curve_key& key)
{
    const bc::data_chunk public_key = key.public_key();
    if (public_key.empty())
    {
        return false;
    }
    bc::payment_address in_address;
    bc::set_public_key(in_address, public_key);
    /* Able to create payment_address? */
    if (in_address.version() == bc::payment_address::invalid_version)
    {
        return false;
    }
    /* Create the input script */
    bc::script_type script_code = build_pubkey_hash_script(in_address.hash());

    size_t input_index = 0;
    bc::hash_digest tx_hash =
        bc::script_type::generate_signature_hash(utx.tx, input_index, script_code, 1);
    if (tx_hash == bc::null_hash)
    {
        return false;
    }
    bc::data_chunk signature = key.sign(tx_hash);
    /* Why does sx do this? */
    signature.push_back(0x01);
    return true;
}

/* TODO: this is a big todo */
bool picker::send(bc::transaction_type& tx)
{
    return true;
}

bc::script_type picker::build_pubkey_hash_script(const bc::short_hash& pubkey_hash)
{
    bc::script_type result;
    result.push_operation({bc::opcode::dup, bc::data_chunk()});
    result.push_operation({bc::opcode::hash160, bc::data_chunk()});
    result.push_operation({bc::opcode::special,
        bc::data_chunk(pubkey_hash.begin(), pubkey_hash.end())});
    result.push_operation({bc::opcode::equalverify, bc::data_chunk()});
    result.push_operation({bc::opcode::checksig, bc::data_chunk()});
    return result;
}

}

