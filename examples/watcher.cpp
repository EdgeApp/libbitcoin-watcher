#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <bitcoin/watcher/watcher.hpp>

using std::placeholders::_1;
using std::placeholders::_2;

/**
 * Command-line interface to the wallet watcher service.
 */
class cli
{
public:
    ~cli();
    cli();

    int run();

private:
    void cmd_exit();
    void cmd_help();
    void cmd_connect(std::stringstream& args);
    void cmd_disconnect(std::stringstream& args);
    void cmd_watch(std::stringstream& args);
    void cmd_height();
    void cmd_status();
    void cmd_tx_height(std::stringstream& args);
    void cmd_tx_watch(std::stringstream& args);
    void cmd_tx_dump(std::stringstream& args);
    void cmd_tx_send(std::stringstream& args);
    void cmd_prioritize(std::stringstream& args);
    void cmd_utxos(std::stringstream& args);
    void cmd_save(std::stringstream& args);
    void cmd_load(std::stringstream& args);
    void cmd_dump(std::stringstream& args);

    void loop();

    void callback(const libbitcoin::transaction_type& tx);
    void send_callback(std::error_code error, const bc::transaction_type& tx);

    bc::hash_digest read_txid(std::stringstream& args);
    bool read_address(std::stringstream& args, bc::payment_address& out);
    bool read_filename(std::stringstream& args, std::string& out);

    libwallet::watcher watcher;
    std::thread looper_;
    bool done_;
};

cli::~cli()
{
}

cli::cli()
  : watcher(),
    looper_([this](){ loop(); }),
    done_(false)
{
    auto cb = std::bind(&cli::callback, this, _1);
    watcher.set_callback(cb);

    auto send_cb = std::bind(&cli::send_callback, this, _1, _2);
    watcher.set_tx_sent_callback(send_cb);
}

int cli::run()
{
    std::cout << "type \"help\" for instructions" << std::endl;

    while (!done_)
    {
        // Read a line:
        std::cout << "> " << std::flush;
        char line[1000];
        std::cin.getline(line, sizeof(line));

        // Extract the command:
        std::stringstream reader(line);
        std::string command;
        reader >> command;

        if (command == "exit")              cmd_exit();
        else if (command == "help")         cmd_help();
        else if (command == "connect")      cmd_connect(reader);
        else if (command == "disconnect")   cmd_disconnect(reader);
        else if (command == "height")       cmd_height();
        else if (command == "status")       cmd_status();
        else if (command == "watch")        cmd_watch(reader);
        else if (command == "txheight")     cmd_tx_height(reader);
        else if (command == "txwatch")      cmd_tx_watch(reader);
        else if (command == "txdump")       cmd_tx_dump(reader);
        else if (command == "txsend")       cmd_tx_send(reader);
        else if (command == "prioritize")   cmd_prioritize(reader);
        else if (command == "utxos")        cmd_utxos(reader);
        else if (command == "save")         cmd_save(reader);
        else if (command == "load")         cmd_load(reader);
        else if (command == "dump")         cmd_dump(reader);
        else
            std::cout << "unknown command " << command << std::endl;
    }
    return 0;
}

void cli::cmd_exit()
{
    std::cout << "waiting for thread to stop..." << std::endl;
    watcher.stop();
    looper_.join();
    done_ = true;
}

void cli::cmd_help()
{
    std::cout << "commands:" << std::endl;
    std::cout << "  exit              - leave the program" << std::endl;
    std::cout << "  help              - this menu" << std::endl;
    std::cout << "  connect <server>  - connect to obelisk server" << std::endl;
    std::cout << "  disconnect        - stop talking to the obelisk server" << std::endl;
    std::cout << "  height            - get the current blockchain height" << std::endl;
    std::cout << "  status            - get the watcher state" << std::endl;
    std::cout << "  watch <address> [poll ms] - watch an address" << std::endl;
    std::cout << "  txheight <hash>   - get a transaction's height" << std::endl;
    std::cout << "  txwatch <hash>    - manually watch a specific transaction" << std::endl;
    std::cout << "  txdump <hash>     - show the contents of a transaction" << std::endl;
    std::cout << "  txsend <hash>     - push a transaction to the server" << std::endl;
    std::cout << "  prioritize [address] - check an address more frequently" << std::endl;
    std::cout << "  utxos [address]   - get utxos for an address" << std::endl;
    std::cout << "  save <filename>   - dump the database to disk" << std::endl;
    std::cout << "  load <filename>   - load the database from disk" << std::endl;
    std::cout << "  dump [filename]   - display the database contents" << std::endl;
}

void cli::cmd_connect(std::stringstream& args)
{
    std::string arg;
    args >> arg;
    if (!arg.size())
    {
        std::cout << "no server given" << std::endl;
        return;
    }
    std::cout << "connecting to " << arg << std::endl;

    watcher.connect(arg);
}

void cli::cmd_disconnect(std::stringstream& args)
{
    watcher.disconnect();
}

void cli::cmd_height()
{
    std::cout << watcher.get_last_block_height() << std::endl;
}

void cli::cmd_status()
{
    switch (watcher.get_status())
    {
        case libwallet::watcher::watcher_syncing:
            std::cout << "Syncing" << std::endl;
            break;
        case libwallet::watcher::watcher_sync_ok:
            std::cout << "OK" << std::endl;
            break;
    }
}

void cli::cmd_tx_height(std::stringstream& args)
{
    bc::hash_digest txid = read_txid(args);
    if (txid == bc::null_hash)
        return;
    int height;
    if (watcher.get_tx_height(txid, height))
        std::cout << height << std::endl;
    else
        std::cout << "Synchronizing..." << std::endl;
}

void cli::cmd_tx_watch(std::stringstream& args)
{
    bc::hash_digest txid = read_txid(args);
    if (txid == bc::null_hash)
        return;
    watcher.watch_tx_mem(txid);
}

void cli::cmd_tx_dump(std::stringstream& args)
{
    bc::hash_digest txid = read_txid(args);
    if (txid == bc::null_hash)
        return;
    bc::transaction_type tx = watcher.find_tx(txid);

    std::basic_ostringstream<uint8_t> stream;
    auto serial = bc::make_serializer(std::ostreambuf_iterator<uint8_t>(stream));
    serial.set_iterator(satoshi_save(tx, serial.iterator()));
    auto str = stream.str();
    std::cout << bc::encode_hex(str) << std::endl;
}

void cli::cmd_tx_send(std::stringstream& args)
{
    std::string arg;
    args >> arg;
    bc::data_chunk data = bc::decode_hex(arg);
    bc::transaction_type tx;
    try
    {
        bc::satoshi_load(data.begin(), data.end(), tx);
    }
    catch (bc::end_of_stream)
    {
        std::cout << "not a valid transaction" << std::endl;
        return;
    }
    watcher.send_tx(tx);
}

void cli::cmd_watch(std::stringstream& args)
{
    bc::payment_address address;
    if (!read_address(args, address))
        return;
    unsigned poll_ms = 10000;
    args >> poll_ms;
    if (poll_ms < 500)
    {
        std::cout << "warning: poll too short, setting to 500ms" << std::endl;
        poll_ms = 500;
    }
    watcher.watch_address(address, poll_ms);
}

void cli::cmd_prioritize(std::stringstream& args)
{
    bc::payment_address address;
    std::string arg;
    args >> arg;
    if (arg.size())
        address.set_encoded(arg);
    watcher.prioritize_address(address);
}

void cli::cmd_utxos(std::stringstream& args)
{
    bc::output_info_list utxos;
    bc::payment_address address;
    std::string arg;
    args >> arg;
    if (arg.size() && address.set_encoded(arg))
        utxos = watcher.get_utxos(address);
    else
        utxos = watcher.get_utxos();

    size_t total = 0;

    for (auto& utxo: utxos)
    {
        std::cout << bc::encode_hex(utxo.point.hash) << ":" <<
            utxo.point.index << std::endl;
        auto tx = watcher.find_tx(utxo.point.hash);
        auto& output = tx.outputs[utxo.point.index];
        bc::payment_address to_address;
        if (bc::extract(to_address, output.script))
            std::cout << "address: " << to_address.encoded() << " ";
        std::cout << "value: " << output.value << std::endl;
        total += output.value;
    }
    std::cout << "total: " << total << std::endl;
}

void cli::cmd_save(std::stringstream& args)
{
    std::string filename;
    if (!read_filename(args, filename))
        return;

    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "cannot open " << filename << std::endl;
        return;
    }

    auto db = watcher.serialize();
    file.write(reinterpret_cast<const char*>(db.data()), db.size());
    file.close();
}

void cli::cmd_load(std::stringstream& args)
{
    std::string filename;
    if (!read_filename(args, filename))
        return;

    std::ifstream file(filename, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        std::cerr << "cannot open " << filename << std::endl;
        return;
    }

    std::streampos size = file.tellg();
    uint8_t *data = new uint8_t[size];
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data), size);
    file.close();

    if (!watcher.load(bc::data_chunk(data, data + size)))
        std::cerr << "error while loading data" << std::endl;
}

void cli::cmd_dump(std::stringstream& args)
{
    std::string filename;
    args >> filename;
    if (filename.size())
    {
        std::ofstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "cannot open " << filename << std::endl;
            return;
        }
        watcher.dump(file);
    }
    else
        watcher.dump(std::cout);
}

void cli::loop()
{
    watcher.loop();
}

void cli::callback(const libbitcoin::transaction_type& tx)
{
    auto txid = libbitcoin::encode_hex(libbitcoin::hash_transaction(tx));
    std::cout << "got transaction " << txid << std::endl;
}

void cli::send_callback(std::error_code error, const bc::transaction_type& tx)
{
    if (error)
        std::cout << "failed to send transaction" << std::endl;
    else
        std::cout << "sent transaction" << std::endl;
}

bc::hash_digest cli::read_txid(std::stringstream& args)
{
    std::string arg;
    args >> arg;
    if (!arg.size())
    {
        std::cout << "no txid given" << std::endl;
        return bc::null_hash;
    }
    return bc::decode_hash(arg);
}

bool cli::read_address(std::stringstream& args, bc::payment_address& out)
{
    std::string arg;
    args >> arg;
    if (!arg.size())
    {
        std::cout << "no address given" << std::endl;
        return false;
    }

    if (!out.set_encoded(arg))
    {
        std::cout << "invalid address " << arg << std::endl;
        return false;
    }
    return true;
}

bool cli::read_filename(std::stringstream& args, std::string& out)
{
    args >> out;
    if (!out.size())
    {
        std::cout << "no file name given" << std::endl;
        return false;
    }
    return true;
}

int main()
{
    cli c;
    return c.run();
}
