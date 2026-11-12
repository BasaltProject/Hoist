#pragma once

#include <basalt/hoist/config/Config.hpp>

#include <filesystem>

namespace basalt::hoist::runtime {

enum ExitCode : int {
    kExitOk = 0,
    kExitError = 1,
    kExitUsage = 2,
    kExitBadConfig = 3,
    kExitBusy = 4,
    kExitCrashLoop = 5,
    kExitInstallFailed = 6,
};

struct RunOptions {
    std::filesystem::path directory = ".";
    std::filesystem::path configDirectory; // empty means <directory>/config
    config::Target target = config::Target::Server;
};

int runSupervisor(const RunOptions& options);

std::filesystem::path requestsDirectory(const std::filesystem::path& directory);

} // namespace basalt::hoist::runtime
