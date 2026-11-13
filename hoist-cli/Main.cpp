#include "Menu.hpp"

#include <basalt/hoist/runtime/Commands.hpp>
#include <basalt/hoist/runtime/Log.hpp>

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifndef HOIST_VERSION
#define HOIST_VERSION "26.3.0-dev"
#endif

using namespace basalt::hoist;

namespace {

constexpr const char* kHelp = R"(Basalt Hoist: keeps Basalt Server or Basalt Proxy up to date.

Usage: hoist <command> [options]   (without a command Hoist asks on a terminal)

Commands:
  run        Start the product and keep it running, checking for updates in the background
  init       Write the default configuration files and stop
  check      Look for a new version now and say what Hoist would do with it
  status     Show what is installed and how Hoist is set up
  update     Apply the pending update now (asks the running Hoist, or does it when none runs)
  rollback   Go back to the previous version
  backup     create [label] | list | restore <name|latest> | delete <name>
  version    Print the version of Hoist

Options:
  -C, --directory <dir>   The folder of the product (default: the current one)
      --config <dir>      The configuration folder (default: <directory>/config/hoist)
      --target <product>  "server" or "proxy", used when the configuration is first written
  -h, --help              Show this help

One folder holds one product and one Hoist. Configuration lives in config/hoist/*.toml.
)";

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string command;
    bool commandGiven = false;
    runtime::RunOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto value = [&]() -> const std::string* { return i + 1 < args.size() ? &args[++i] : nullptr; };
        if (arg == "-h" || arg == "--help" || arg == "help") {
            std::cout << kHelp;
            return runtime::kExitOk;
        }
        if (arg == "--version" || arg == "version") {
            std::cout << "Basalt Hoist " << HOIST_VERSION << "\n";
            return runtime::kExitOk;
        }
        if (arg == "-C" || arg == "--directory" || arg == "--config" || arg == "--target") {
            const std::string* next = value();
            if (!next) {
                runtime::logError("The option " + arg + " needs a value.");
                return runtime::kExitUsage;
            }
            if (arg == "--config") {
                options.configDirectory = *next;
            } else if (arg == "--target") {
                if (*next != "server" && *next != "proxy") {
                    runtime::logError("--target must be server or proxy.");
                    return runtime::kExitUsage;
                }
                options.target = *next == "proxy" ? config::Target::Proxy : config::Target::Server;
                options.targetChosen = true;
            } else {
                options.directory = *next;
            }
        } else if (!arg.empty() && arg.front() != '-' && !commandGiven) {
            command = arg;
            commandGiven = true;
        } else if (!arg.empty() && arg.front() != '-' && command == "backup") {
            options.arguments.push_back(arg);
        } else {
            runtime::logError("Unknown argument: " + arg + ". See `hoist --help`.");
            return runtime::kExitUsage;
        }
    }
    runtime::detectTarget(options);
    if (!commandGiven) {
        return cli::runMenu(options, HOIST_VERSION, kHelp);
    }
    if (command == "run") {
        return runtime::runSupervisor(options);
    }
    if (command == "init") {
        return runtime::commandInit(options);
    }
    if (command == "check") {
        return runtime::commandCheck(options);
    }
    if (command == "status") {
        return runtime::commandStatus(options);
    }
    if (command == "update") {
        return runtime::commandUpdate(options);
    }
    if (command == "rollback") {
        return runtime::commandRollback(options);
    }
    if (command == "backup") {
        return runtime::commandBackup(options);
    }
    runtime::logError("Unknown command: " + command + ". See `hoist --help`.");
    return runtime::kExitUsage;
}
