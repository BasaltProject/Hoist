#pragma once

#include <atomic>
#include <chrono>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace basalt::hoist::process {

struct LaunchSpec {
    std::string command;
    std::vector<std::string> args;
    std::filesystem::path workingDirectory;
};

struct ExitStatus {
    int code = 0;
    bool signaled = false;
    int signal = 0;

    bool clean() const { return !signaled && code == 0; }
};

class Process {
public:
    virtual ~Process() = default;

    virtual long id() const = 0;
    virtual std::optional<ExitStatus> poll() = 0;
    virtual bool writeLine(std::string_view line) = 0;
    // The polite end: ask it to finish. Where the platform has no such signal this does nothing.
    virtual void terminate() = 0;
    virtual void kill() = 0;
};

std::expected<std::unique_ptr<Process>, std::string> spawn(const LaunchSpec& spec);

// Copies our own input to the child until it is gone or `stop` is set.
// A line the filter takes (returns true) is not passed on to the child.
using LineFilter = std::function<bool(std::string_view)>;

void forwardInput(Process& child, const std::atomic<bool>& stop, const LineFilter& filter = {});

// Ends the thread running forwardInput, waking it from a blocked read where the platform needs it.
void stopInput(std::jthread& thread, std::atomic<bool>& stop);

} // namespace basalt::hoist::process
