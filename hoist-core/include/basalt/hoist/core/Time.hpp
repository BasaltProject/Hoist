#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace basalt::hoist::core {

struct TimeInfo {
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int weekday = 4; // 1 is Monday, 7 is Sunday
    std::int64_t epoch = 0;

    int minutesOfDay() const { return hour * 60 + minute; }

    int date() const { return year * 10000 + month * 100 + day; }
};

// "local" is the process zone, "UTC" is UTC, anything else is an IANA name (POSIX hosts only).
bool isKnownZone(std::string_view zone);
std::optional<TimeInfo> toTimeInfo(std::int64_t epoch, std::string_view zone);
std::int64_t epochFromUtc(int year, int month, int day, int hour, int minute, int second);
std::optional<std::int64_t> parseIso8601(std::string_view text);
bool parseClock(std::string_view text, int& hour, int& minute);

class DailyRestart {
public:
    DailyRestart(int hour, int minute, const TimeInfo& startedAt);
    bool due(const TimeInfo& now);

private:
    int minute_;
    int lastFired_ = 0;
};

class Countdown {
public:
    void begin(std::int64_t now, int seconds);

    void cancel() { active_ = false; }

    bool active() const { return active_; }

    int remaining(std::int64_t now) const;

    bool finished(std::int64_t now) const { return active_ && remaining(now) <= 0; }

    std::optional<int> nextAnnouncement(std::int64_t now);

private:
    bool active_ = false;
    std::int64_t deadline_ = 0;
    int total_ = 0;
    int lastAnnounced_ = 0;
};

} // namespace basalt::hoist::core
