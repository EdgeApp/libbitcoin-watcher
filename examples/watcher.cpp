#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <bitcoin/client/watcher.hpp>

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
    void cmd_tx_height(std::stringstream& args);
    void cmd_tx_watch(std::stringstream& args);
    void cmd_prioritize(std::stringstream& args);
    void cmd_utxos(std::stringstream& args);
    void cmd_save(std::stringstream& args);
    void cmd_load(std::stringstream& args);

    void callback(const libbitcoin::transaction_type& tx);

    bc::hash_digest read_txid(std::stringstream& args);
    bool read_address(std::stringstream& args, bc::payment_address& out);
    bool read_filename(std::stringstream& args, std::string& out);

    libwallet::watcher watcher;
    bool done_;
};

cli::~cli()
{
}

cli::cli()
  : done_(false)
{
    libwallet::watcher::callback cb =
        [this](const libbitcoin::transaction_type& tx)
        {
            callback(tx);
        };
    watcher.set_callback(cb);
}

int cli::run()
{
    while (!done_)
    {
        std::cout << "> " << std::flush;

        char line[1000];
        std::cin.getline(line, sizeof(line));
        std::stringstream reader(line);
        std::string command;
        reader >> command;

        if (command == "exit")              cmd_exit();
        else if (command == "help")         cmd_help();
        else if (command == "connect")      cmd_connect(reader);
        else if (command == "disconnect")   cmd_disconnect(reader);
        else if (command == "watch")        cmd_watch(reader);
        else if (command == "height")       cmd_height();
        else if (command == "txheight")     cmd_tx_height(reader);
        else if (command == "txwatch")      cmd_tx_watch(reader);
        else if (command == "prioritize")   cmd_prioritize(reader);
        else if (command == "utxos")        cmd_utxos(reader);
        else if (command == "save")         cmd_save(reader);
        else if (command == "load")         cmd_load(reader);
        else
            std::cout << "unknown command " << command << std::endl;
    }
    return 0;
}

void cli::cmd_exit()
{
    std::cout << "waiting for thread to stop..." << std::endl;
    done_ = true;
}

void cli::cmd_help()
{
    std::cout << "commands:" << std::endl;
    std::cout << "  exit              - leave the program" << std::endl;
    std::cout << "  help              - this menu" << std::endl;
    std::cout << "  connect <server>  - connect to obelisk server" << std::endl;
    std::cout << "  disconnect        - stop talking to the obelisk server" << std::endl;
    std::cout << "  watch <address>   - watch an address" << std::endl;
    std::cout << "  prioritize [<address>] - check an address more frequently" << std::endl;
    std::cout << "  utxos <address>   - get utxos for an address" << std::endl;
    std::cout << "  save <filename>   - dump the database to disk" << std::endl;
    std::cout << "  load <filename>   - load the database from disk" << std::endl;
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

void cli::cmd_tx_height(std::stringstream& args)
{
    bc::hash_digest txid = read_txid(args);
    if (txid == bc::null_hash)
        return;
    std::cout << watcher.get_tx_height(txid) << std::endl;
}

void cli::cmd_tx_watch(std::stringstream& args)
{
    bc::hash_digest txid = read_txid(args);
    if (txid == bc::null_hash)
        return;
    watcher.watch_tx_mem(txid);
}

void cli::cmd_watch(std::stringstream& args)
{
    bc::payment_address address;
    if (!read_address(args, address))
        return;
    watcher.watch_address(address);
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
    bc::payment_address address;
    if (!read_address(args, address))
        return;

    auto list = watcher.get_utxos(address);
    for (auto& utxo: list)
        std::cout << bc::encode_hex(utxo.point.hash) << ":" <<
            utxo.point.index << std::endl;
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

void cli::callback(const libbitcoin::transaction_type& tx)
{
    auto txid = libbitcoin::encode_hex(libbitcoin::hash_transaction(tx));
    std::cout << "got transaction " << txid << std::endl;
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
    std::cout << "type \"help\" for instructions" << std::endl;
    return c.run();
}
