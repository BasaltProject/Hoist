#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <basalt/hoist/runtime/Scheduler.hpp>

#include <doctest/doctest.h>

using namespace basalt::hoist;

namespace {

core::TimeInfo at(int hour, int minute) {
    return *core::toTimeInfo(core::epochFromUtc(2026, 11, 12, hour, minute, 0), "UTC");
}

} // namespace

TEST_CASE("a condition fires only after it has held for its grace time, and not without an update") {
    config::ScheduleConfig schedule;
    schedule.conditions.push_back({"quiet", "%time% >= 03:00 && %time% < 06:00 && %players_online% < 5", 120, {}});
    std::vector<std::string> reports;
    runtime::Scheduler scheduler(schedule, at(1, 0), [&](const std::string& text) { reports.push_back(text); });

    expr::Placeholders values;
    values.set("time", 3.0 * 60);
    values.set("players_online", 2.0);
    CHECK_FALSE(scheduler.poll(at(3, 0), values, false));
    CHECK_FALSE(scheduler.poll(at(3, 0), values, true));
    CHECK_FALSE(scheduler.poll(at(3, 1), values, true));
    auto trigger = scheduler.poll(at(3, 2), values, true);
    REQUIRE(trigger.has_value());
    CHECK(trigger->name == "quiet");

    values.unset("players_online");
    CHECK_FALSE(scheduler.stillHolds(*trigger, values));
    CHECK(reports.size() == 1);
}
