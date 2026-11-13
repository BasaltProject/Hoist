#include <basalt/hoist/runtime/Log.hpp>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace basalt::hoist::runtime {
namespace {

std::mutex logMutex;

constexpr std::string_view kReset = "\x1B[0m";
constexpr std::string_view kDim = "\x1B[2m";
constexpr std::string_view kRed = "\x1B[31m";
constexpr std::string_view kGreen = "\x1B[32m";
constexpr std::string_view kYellow = "\x1B[33m";

bool colorEnabled() {
    static const bool enabled = [] {
        if (std::getenv("NO_COLOR") != nullptr) {
            return false;
        }
#ifdef _WIN32
        const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        return _isatty(_fileno(stdout)) != 0 && GetConsoleMode(output, &mode) != 0 &&
               SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
        return isatty(STDOUT_FILENO) != 0;
#endif
    }();
    return enabled;
}

void write(std::string_view level, std::string_view colour, std::string_view message) {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[24];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);
    std::string line;
    if (colorEnabled()) {
        const bool loud = level != "INFO";
        line = std::string(kDim) + "[" + stamp + "]" + std::string(kReset) + " " + std::string(colour) + "[" +
               std::string(level) + "]" + std::string(kReset) + " " + std::string(kDim) +
               "[Hoist]:" + std::string(kReset) + " " + (loud ? std::string(colour) : std::string()) +
               std::string(message) + std::string(kReset);
    } else {
        line = std::string("[") + stamp + "] [" + std::string(level) + "] [Hoist]: " + std::string(message);
    }
    std::lock_guard lock(logMutex);
    std::fprintf(stdout, "%s\n", line.c_str());
    std::fflush(stdout);
}

} // namespace

void logInfo(std::string_view message) { write("INFO", kGreen, message); }

void logWarn(std::string_view message) { write("WARN", kYellow, message); }

void logError(std::string_view message) { write("ERROR", kRed, message); }

} // namespace basalt::hoist::runtime
