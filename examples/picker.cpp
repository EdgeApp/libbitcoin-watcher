
#include <iostream>
#include <string>
#include <sstream>

#include <bitcoin/bitcoin.hpp>
#include <bitcoin/picker.hpp>

static void
create_tx(bc::transaction_type& tx,
          std::string ia, uint32_t idx,
          std::string oa, uint64_t oamt);

static bool
create_out_script(bc::script_type& out_script,
                  const bc::payment_address addr);

static bc::script_type
build_pubkey_hash_script(const bc::short_hash& pubkey_hash);

static bc::script_type
build_script_hash_script(const bc::short_hash& script_hash);

static bc::transaction_output_type
make_output(uint64_t amount, bc::payment_address addr);

static void
test_walletwatcher_1()
{
    bc::payment_address change("18tLAQczRkDAh95xxEzvFKaaX9yHi5iNq6");

    bc::elliptic_curve_key mykey;
    mykey.new_keypair();

    bc::payment_address myaddr;
    bc::set_public_key(myaddr, mykey.public_key());
    std::cout << "My Payment Address: " << myaddr.encoded() << std::endl;
    std::cout << "My Change Address: " << change.encoded() << std::endl;

    bc::payment_address airbitz;
    airbitz.set_encoded("16s85X2NNnX7b6kinLzZDWXgc9CYRrm961");

    bc::payment_address myfriend;
    myfriend.set_encoded("115BsxMQvVgJ7ZP4vrFrG6hNDUy6SypCi8");

    bc::transaction_type tx1;
    create_tx(tx1, "97e06e49dfdd26c5a904670971ccf4c7fe7d9da53cb379bf9b442fc9427080b3", 1,
                   myaddr.encoded(), 1000);
    bc::transaction_type tx2;
    create_tx(tx2, "97e06e49dfdd26c5a904670971ccf4c7fe7d9da53cb379bf9b442fc9427080b3", 1,
                   myaddr.encoded(), 2000);
    bc::transaction_type tx3;
    create_tx(tx3, "97e06e49dfdd26c5a904670971ccf4c7fe7d9da53cb379bf9b442fc9427080b3", 1,
                   myaddr.encoded(), 3000);

    libwallet::picker ww;
    ww.watch_addr(myaddr.encoded());
    ww.add_tx(tx1);
    ww.add_tx(tx2);
    ww.add_tx(tx3);

    // Check unspent
    bc::output_info_list unspent = ww.unspent_outputs(myaddr.encoded());
    bc::output_info_list::iterator it = unspent.begin();
    std::cout << "Unspent count: " << unspent.size() << std::endl;
    for ( ; it != unspent.end(); ++it)
    {
        std::cout << "Tx Hash: " << bc::encode_hex(it->point.hash) << std::endl;
        std::cout << "\tIdx: " << it->point.index << std::endl;
        std::cout << "\tValue: " << it->value << std::endl;
    }

    uint64_t total = 5500;
    bc::transaction_output_list outputs;
    // To My Friend
    outputs.push_back(make_output(4000, myfriend));
    // To Airbitz
    outputs.push_back(make_output(1000, airbitz));

    libwallet::unsigned_transaction_type utx;
    libwallet::fee_schedule sched{1000};
    if (ww.create_unsigned_tx(utx, myaddr, total,
                              change.encoded(), sched, outputs))
    {
        std::cout << "Created unsigned tx!" << std::endl;
        if (ww.sign_and_send(utx, mykey))
        {
            std::cout << "Signed tx!" << std::endl;
            std::cout << "Fees: " << utx.fees << std::endl;
            std::cout << bc::pretty(utx.tx) << std::endl;
        }
        else
        {
            std::cout << "Failed to sign TX" << std::endl;
        }
    }
    else
    {
        std::cout << "FAILED to create unsigned tx!" << std::endl;
    }
}

int main(int argc, const char *argv[])
{
    printf("test_walletwatcher_1();\n");
    test_walletwatcher_1();
    return 0;
}

static
bc::transaction_output_type make_output(uint64_t amount, bc::payment_address addr)
{
    bc::transaction_output_type t;
    t.value = amount;
     create_out_script(t.script, addr);
    return t;
}

static
void create_tx(bc::transaction_type& tx,
               std::string ia, uint32_t  idx,
               std::string oa, uint64_t oamt)
{
    tx.version = 1;
    tx.locktime = 0;

    bc::transaction_input_type input;
    bc::output_point& prevout = input.previous_output;
    auto chunk = bc::decode_hex(ia);
    std::copy(chunk.begin(), chunk.end(), prevout.hash.begin());
    prevout.index = idx;

    bc::payment_address addr;
    addr.set_encoded(oa);

    bc::transaction_output_type output;
    output.value = oamt;
    create_out_script(output.script, addr);

    tx.outputs.push_back(output);
    tx.inputs.push_back(input);
}

static
bool create_out_script(bc::script_type& out_script,
                       const bc::payment_address addr)
{
    switch (addr.version())
    {
        case bc::payment_address::pubkey_version:
            out_script = build_pubkey_hash_script(addr.hash());
            return true;

        case bc::payment_address::script_version:
            out_script = build_script_hash_script(addr.hash());
            return true;
    }
    return false;
}

static
bc::script_type build_pubkey_hash_script(const bc::short_hash& pubkey_hash)
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

static
bc::script_type build_script_hash_script(const bc::short_hash& script_hash)
{
    bc::script_type result;
    result.push_operation({bc::opcode::hash160, bc::data_chunk()});
    result.push_operation({bc::opcode::special,
        bc::data_chunk(script_hash.begin(), script_hash.end())});
    result.push_operation({bc::opcode::equal, bc::data_chunk()});
    return result;
}

