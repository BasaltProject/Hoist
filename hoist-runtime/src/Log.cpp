#include <basalt/hoist/runtime/Log.hpp>

#include <cstdio>
#include <ctime>
#include <mutex>

namespace basalt::hoist::runtime {
namespace {

std::mutex logMutex;

void write(const char* level, std::string_view message) {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", &tm);
    std::lock_guard lock(logMutex);
    std::fprintf(stdout, "[%s] [Hoist/%s] %.*s\n", stamp, level, static_cast<int>(message.size()), message.data());
    std::fflush(stdout);
}

} // namespace

void logInfo(std::string_view message) { write("INFO", message); }

void logWarn(std::string_view message) { write("WARN", message); }

void logError(std::string_view message) { write("ERROR", message); }

} // namespace basalt::hoist::runtime
