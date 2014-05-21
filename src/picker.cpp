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
#include <bitcoin/client/picker.hpp>

#include <unistd.h>
#include <iostream>
#include <bitcoin/bitcoin.hpp>
#include <wallet/transaction.hpp>

namespace libwallet {

static script_type build_pubkey_hash_script(const short_hash& pubkey_hash);

BC_API bool make_tx(
             watcher& watcher,
             const payment_address& fromAddr,
             const payment_address& changeAddr,
             int64_t amountSatoshi,
             fee_schedule& sched,
             transaction_output_list& outputs,
             unsigned_transaction_type& utx)
{
    output_info_list unspent = watcher.get_utxos(fromAddr);
    select_outputs_result os = select_outputs(unspent, amountSatoshi);
    /* Do we have the funds ? */
    if (os.points.size() <= 0)
        return false;

    utx.tx.version = 1;
    utx.tx.locktime = 0;
    auto it = os.points.begin();
    for (; it != os.points.end(); it++)
    {
        transaction_input_type input;
        input.previous_output.index = it->index;
        input.previous_output.hash = it->hash;
        utx.tx.inputs.push_back(input);
    }
    utx.tx.outputs = outputs;
    /* If change is needed, add the change address */
    if (os.change > 0)
    {
        transaction_output_type change;
        change.value = os.change;
        change.script = build_pubkey_hash_script(changeAddr.hash());
        utx.tx.outputs.push_back(change);
    }
    /* Calculate fees with this transaction */
    utx.fees = sched.satoshi_per_kb * (satoshi_raw_size(utx.tx) / 1024);
    return true;
}

BC_API bool sign_send_tx(
                  watcher& watcher,
                  unsigned_transaction_type& utx,
                  const elliptic_curve_key& key)
{
    const data_chunk public_key = key.public_key();
    if (public_key.empty())
    {
        return false;
    }
    payment_address in_address;
    set_public_key(in_address, public_key);
    /* Able to create payment_address? */
    if (in_address.version() == payment_address::invalid_version)
    {
        return false;
    }
    /* Create the input script */
    script_type script_code = build_pubkey_hash_script(in_address.hash());

    size_t input_index = 0;
    hash_digest tx_hash =
        script_type::generate_signature_hash(utx.tx, input_index, script_code, 1);
    if (tx_hash == null_hash)
    {
        return false;
    }
    data_chunk signature = key.sign(tx_hash);
    /* Why does sx do this? */
    signature.push_back(0x01);

    watcher.send_tx(utx.tx);
    return true;
}

static script_type build_pubkey_hash_script(const short_hash& pubkey_hash)
{
    script_type result;
    result.push_operation({opcode::dup, data_chunk()});
    result.push_operation({opcode::hash160, data_chunk()});
    result.push_operation({opcode::special,
            data_chunk(pubkey_hash.begin(), pubkey_hash.end())});
    result.push_operation({opcode::equalverify, data_chunk()});
    result.push_operation({opcode::checksig, data_chunk()});
    return result;
}

}

