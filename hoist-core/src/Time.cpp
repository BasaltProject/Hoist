#include <basalt/hoist/core/Time.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>

namespace basalt::hoist::core {
namespace {

std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

TimeInfo fromTm(const std::tm& tm, std::int64_t epoch) {
    TimeInfo info;
    info.year = tm.tm_year + 1900;
    info.month = tm.tm_mon + 1;
    info.day = tm.tm_mday;
    info.hour = tm.tm_hour;
    info.minute = tm.tm_min;
    info.second = tm.tm_sec;
    info.weekday = tm.tm_wday == 0 ? 7 : tm.tm_wday;
    info.epoch = epoch;
    return info;
}

std::mutex zoneMutex;

std::optional<int> parseDigits(std::string_view text) {
    int value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

bool isKnownZone(std::string_view zone) {
    if (zone.empty() || zone == "local" || zone == "UTC") {
        return true;
    }
#ifdef _WIN32
    return false;
#else
    if (zone.find("..") != std::string_view::npos || zone.starts_with('/')) {
        return false;
    }
    const char* dir = std::getenv("TZDIR");
    return std::filesystem::is_regular_file(std::filesystem::path(dir ? dir : "/usr/share/zoneinfo") /
                                            std::string(zone));
#endif
}

std::optional<TimeInfo> toTimeInfo(std::int64_t epoch, std::string_view zone) {
    if (!isKnownZone(zone)) {
        return std::nullopt;
    }
    const auto raw = static_cast<std::time_t>(epoch);
    std::tm tm{};
#ifdef _WIN32
    if (zone == "UTC" ? gmtime_s(&tm, &raw) != 0 : localtime_s(&tm, &raw) != 0) {
        return std::nullopt;
    }
    return fromTm(tm, epoch);
#else
    if (zone == "UTC") {
        gmtime_r(&raw, &tm);
        return fromTm(tm, epoch);
    }
    if (zone.empty() || zone == "local") {
        localtime_r(&raw, &tm);
        return fromTm(tm, epoch);
    }
    std::lock_guard lock(zoneMutex);
    const char* previous = std::getenv("TZ");
    const std::string saved = previous ? previous : "";
    setenv("TZ", std::string(zone).c_str(), 1);
    tzset();
    localtime_r(&raw, &tm);
    if (previous) {
        setenv("TZ", saved.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return fromTm(tm, epoch);
#endif
}

std::int64_t epochFromUtc(int year, int month, int day, int hour, int minute, int second) {
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 +
           minute * 60 + second;
}

std::optional<std::int64_t> parseIso8601(std::string_view text) {
    if (text.size() != 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
        text[16] != ':' || text[19] != 'Z') {
        return std::nullopt;
    }
    std::array<std::optional<int>, 6> parts = {parseDigits(text.substr(0, 4)),  parseDigits(text.substr(5, 2)),
                                               parseDigits(text.substr(8, 2)),  parseDigits(text.substr(11, 2)),
                                               parseDigits(text.substr(14, 2)), parseDigits(text.substr(17, 2))};
    if (std::ranges::any_of(parts, [](const auto& part) { return !part; })) {
        return std::nullopt;
    }
    return epochFromUtc(*parts[0], *parts[1], *parts[2], *parts[3], *parts[4], *parts[5]);
}

bool parseClock(std::string_view text, int& hour, int& minute) {
    auto colon = text.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    auto h = parseDigits(text.substr(0, colon));
    auto m = parseDigits(text.substr(colon + 1));
    if (!h || !m || *h < 0 || *h > 23 || *m < 0 || *m > 59) {
        return false;
    }
    hour = *h;
    minute = *m;
    return true;
}

DailyRestart::DailyRestart(int hour, int minute, const TimeInfo& startedAt) : minute_(hour * 60 + minute) {
    if (startedAt.minutesOfDay() >= minute_) {
        lastFired_ = startedAt.date();
    }
}

bool DailyRestart::due(const TimeInfo& now) {
    if (now.date() == lastFired_ || now.minutesOfDay() < minute_) {
        return false;
    }
    lastFired_ = now.date();
    return true;
}

void Countdown::begin(std::int64_t now, int seconds) {
    active_ = true;
    total_ = seconds;
    deadline_ = now + seconds;
    lastAnnounced_ = seconds + 1;
}

int Countdown::remaining(std::int64_t now) const { return static_cast<int>(deadline_ - now); }

std::optional<int> Countdown::nextAnnouncement(std::int64_t now) {
    if (!active_) {
        return std::nullopt;
    }
    static constexpr std::array<int, 8> kSteps = {60, 30, 10, 5, 4, 3, 2, 1};
    const int left = remaining(now);
    int best = 0;
    if (total_ < lastAnnounced_ && total_ >= left) {
        best = total_;
    }
    for (int step : kSteps) {
        if (step < total_ && step < lastAnnounced_ && step >= left) {
            best = std::max(best, step);
        }
    }
    if (best == 0) {
        return std::nullopt;
    }
    lastAnnounced_ = best;
    return std::max(left, 1);
}

} // namespace basalt::hoist::core
