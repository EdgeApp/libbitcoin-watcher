#include <iostream>
#include <string>
#include <sstream>
#include <bitcoin/watcher.hpp>

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
    void cmd_connect(std::stringstream &args);
    void cmd_disconnect(std::stringstream &args);
    void cmd_watch(std::stringstream &args);
    void cmd_forget(std::stringstream &args);

    libwallet::watcher watcher;
    bool done_;
};

cli::~cli()
{
}

cli::cli()
  : done_(false)
{
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
}

void cli::cmd_connect(std::stringstream &args)
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

void cli::cmd_disconnect(std::stringstream &args)
{
    watcher.disconnect();
}

void cli::cmd_watch(std::stringstream &args)
{
    std::string arg;
    args >> arg;
    if (!arg.size())
    {
        std::cout << "no address given" << std::endl;
        return;
    }

    bc::payment_address address;
    if (!address.set_encoded(arg))
    {
        std::cout << "invalid address " << arg << std::endl;
        return;
    }
    watcher.watch_address(address);
}

int main()
{
    cli c;
    std::cout << "type \"help\" for instructions" << std::endl;
    return c.run();
}
