
#include <iostream>
#include <string>
#include <sstream>

#include <bitcoin/bitcoin.hpp>
#include <bitcoin/client.hpp>
#include <wallet/wallet.hpp>

using namespace libwallet;

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


int main(int argc, const char *argv[])
{
    if (argc != 4)
    {
        std::cout << argv[0] << " <private-key> <send-to-address> <amount> " << std::endl;
        exit(EXIT_FAILURE);
    }
    std::string privkey(argv[1]);
    std::string toaddress(argv[2]);
    uint64_t total = atol(argv[3]);
    bool compressed = true;

    if (total <= 0)
    {
        std::cerr << "Invalid amount" << std::endl;
        exit(EXIT_FAILURE);
    }

    bc::secret_parameter secret = bc::decode_hash(privkey);
    if (secret == bc::null_hash)
    {
        secret = libwallet::wif_to_secret(privkey);
        compressed = libwallet::is_wif_compressed(privkey);
    }

    bc::elliptic_curve_key mykey;
    if (!mykey.set_secret(secret, compressed))
    {
        std::cerr << "Invalid private key" << std::endl;
        exit(EXIT_FAILURE);
    }

    bc::payment_address myaddr;
    set_public_key(myaddr, mykey.public_key());

    std::cout << "My Payment Address: " << myaddr.encoded() << std::endl;
    std::cout << "My Change Address: " << myaddr.encoded() << std::endl;

    bc::payment_address friendo;
    if (!friendo.set_encoded(toaddress))
    {
        std::cerr << "Invalid payment address " << std::endl;
        exit(EXIT_FAILURE);
    }

    watcher watcher;
    watcher.watch_address(myaddr.encoded());
    watcher.connect("tcp://obelisk.unsystem.net:9091");
    sleep(100);

    bc::transaction_output_list outputs;
    // To Friend
    outputs.push_back(make_output(total, friendo));
    uint64_t fees = 10000;

    libwallet::unsigned_transaction_type utx;
    libwallet::fee_schedule sched;
    sched.satoshi_per_kb = 1000;

    std::vector<bc::payment_address> addresses;
    addresses.push_back(myaddr);
    if (make_tx(watcher, addresses, myaddr,
                total + fees, sched, outputs, utx))
    {
        std::cout << "Created unsigned tx!" << std::endl;
        std::vector<elliptic_curve_key> keys;
        keys.push_back(mykey);
        if (sign_send_tx(watcher, utx, keys))
        {
            bc::data_chunk raw_tx(satoshi_raw_size(utx.tx));
            bc::satoshi_save(utx.tx, raw_tx.begin());
            std::string encoded(bc::encode_hex(raw_tx));
            std::string pretty(bc::pretty(utx.tx));

            std::cout << "Signed tx!" << std::endl;
            std::cout << "Fees: " << utx.fees << std::endl;
            std::cout << bc::pretty(utx.tx) << std::endl;
            std::cout << std::endl;
            std::cout << bc::encode_hex(raw_tx) << std::endl;

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
    watcher.disconnect();
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

