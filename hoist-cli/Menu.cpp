#include "Menu.hpp"

#include <basalt/hoist/runtime/Commands.hpp>
#include <basalt/hoist/runtime/Log.hpp>

#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace basalt::hoist::cli {
namespace {

bool interactive() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0 && _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0;
#endif
}

bool ask(const std::string& prompt, std::string& answer) {
    std::cout << prompt << std::flush;
    if (!std::getline(std::cin, answer)) {
        std::cout << "\n";
        return false;
    }
    const auto first = answer.find_first_not_of(" \t\r");
    const auto last = answer.find_last_not_of(" \t\r");
    answer = first == std::string::npos ? std::string{} : answer.substr(first, last - first + 1);
    return true;
}

bool configured(const runtime::RunOptions& options) {
    const std::filesystem::path directory =
        options.configDirectory.empty() ? options.directory / "config" / "hoist" : options.configDirectory;
    std::error_code ec;
    return std::filesystem::exists(directory / "launcher.toml", ec);
}

bool chooseProduct(runtime::RunOptions& options) {
    std::cout << "There is no configuration in config/hoist yet.\nWhich product do you want to live in this folder?\n"
                 "  1) Server\n  2) Proxy\n";
    std::string answer;
    while (ask("Choose [1-2]: ", answer)) {
        if (answer == "1" || answer == "2") {
            options.target = answer == "2" ? config::Target::Proxy : config::Target::Server;
            return true;
        }
        std::cout << "That is not an option. Choose 1 or 2.\n";
    }
    return false;
}

int backupMenu(runtime::RunOptions options) {
    std::cout << "  1) create\n  2) list\n  3) restore\n  4) delete\n";
    std::string answer;
    while (ask("Choose [1-4]: ", answer)) {
        if (answer == "2") {
            options.arguments = {"list"};
            return runtime::commandBackup(options);
        }
        if (answer == "1") {
            std::string label;
            if (!ask("Label (optional): ", label)) {
                return runtime::kExitOk;
            }
            options.arguments = {"create"};
            if (!label.empty()) {
                options.arguments.push_back(label);
            }
            return runtime::commandBackup(options);
        }
        if (answer == "3" || answer == "4") {
            const bool restore = answer == "3";
            options.arguments = {"list"};
            runtime::commandBackup(options);
            std::string name;
            if (!ask(restore ? "Name to restore (or latest): " : "Name to delete: ", name) || name.empty()) {
                return runtime::kExitOk;
            }
            if (restore) {
                std::string sure;
                if (!ask("Restoring replaces the current files (a pre-restore copy is kept first). Continue? [y/N]: ",
                         sure) ||
                    (sure != "y" && sure != "Y")) {
                    return runtime::kExitOk;
                }
            }
            options.arguments = {restore ? "restore" : "delete", name};
            return runtime::commandBackup(options);
        }
        std::cout << "That is not an option. Choose a number from 1 to 4.\n";
    }
    return runtime::kExitOk;
}

} // namespace

int runMenu(runtime::RunOptions options, const char* version, const char* help) {
    if (!interactive()) {
        runtime::logError("No command given and there is no interactive terminal to ask on.");
        runtime::logInfo("Start the product and keep it updated: hoist run");
        runtime::logInfo("Write the configuration first: hoist init --target server|proxy");
        runtime::logInfo("Every command and option: hoist --help");
        return runtime::kExitUsage;
    }
    std::cout << "Basalt Hoist " << version
              << "\n\nNo command given. What do you want to do?\n\n"
                 "  1) run        Start the product and keep it updated\n"
                 "  2) init       Write the default configuration files\n"
                 "  3) check      Look for a new version now\n"
                 "  4) status     Show what is installed and how Hoist is set up\n"
                 "  5) update     Apply the pending update now\n"
                 "  6) rollback   Go back to the previous version\n"
                 "  7) backup     Create, list, restore or delete backups\n"
                 "  8) help       Show every command and option\n"
                 "  q) quit\n\n";
    std::string answer;
    while (ask("Choose [1-8, q]: ", answer)) {
        if (answer == "q" || answer == "Q") {
            return runtime::kExitOk;
        }
        if (answer == "1" || answer == "2") {
            if (!configured(options) && !options.targetChosen && !chooseProduct(options)) {
                return runtime::kExitOk;
            }
            return answer == "1" ? runtime::runSupervisor(options) : runtime::commandInit(options);
        }
        if (answer == "3") {
            return runtime::commandCheck(options);
        }
        if (answer == "4") {
            return runtime::commandStatus(options);
        }
        if (answer == "5") {
            return runtime::commandUpdate(options);
        }
        if (answer == "6") {
            return runtime::commandRollback(options);
        }
        if (answer == "7") {
            return backupMenu(options);
        }
        if (answer == "8") {
            std::cout << help;
            return runtime::kExitOk;
        }
        std::cout << "That is not an option. Choose a number from 1 to 8, or q to quit.\n";
    }
    return runtime::kExitOk;
}

} // namespace basalt::hoist::cli
