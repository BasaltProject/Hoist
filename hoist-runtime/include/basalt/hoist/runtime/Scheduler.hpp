#pragma once

#include <basalt/hoist/config/Config.hpp>
#include <basalt/hoist/core/Time.hpp>
#include <basalt/hoist/expr/Expression.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace basalt::hoist::runtime {

struct Trigger {
    enum class Reason { Daily, Immediate, Condition, Manual } reason = Reason::Manual;
    std::string name;
    config::Warning warning;
    int conditionIndex = -1;
    bool applyUpdate = false;
};

class Scheduler {
public:
    using Report = std::function<void(const std::string&)>;

    Scheduler(const config::ScheduleConfig& config, const core::TimeInfo& startedAt, Report report);

    // The first mode that fires. A daily restart fires with or without an update; the others need one.
    std::optional<Trigger> poll(const core::TimeInfo& now, const expr::Placeholders& values, bool updateAvailable);
    bool stillHolds(const Trigger& trigger, const expr::Placeholders& values);

    void suppress(std::int64_t untilEpoch) { suppressedUntil_ = untilEpoch; }

    void resetHolds();

private:
    bool evaluate(std::size_t index, const expr::Placeholders& values);

    config::ScheduleConfig config_;
    core::DailyRestart daily_;
    std::vector<std::optional<expr::Expression>> expressions_;
    std::vector<std::optional<std::int64_t>> holdingSince_;
    std::set<std::string> reported_;
    std::int64_t suppressedUntil_ = 0;
    Report report_;
};

} // namespace basalt::hoist::runtime
