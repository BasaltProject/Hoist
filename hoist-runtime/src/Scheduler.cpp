#include <basalt/hoist/runtime/Scheduler.hpp>

namespace basalt::hoist::runtime {

Scheduler::Scheduler(const config::ScheduleConfig& config, const core::TimeInfo& startedAt, Report report)
    : config_(config), daily_(config.dailyRestart.hour, config.dailyRestart.minute, startedAt),
      holdingSince_(config.conditions.size()), report_(std::move(report)) {
    for (const auto& condition : config_.conditions) {
        auto parsed = expr::Expression::parse(condition.when);
        expressions_.push_back(parsed ? std::optional(std::move(*parsed)) : std::nullopt);
    }
}

void Scheduler::resetHolds() {
    for (auto& since : holdingSince_) {
        since.reset();
    }
}

bool Scheduler::evaluate(std::size_t index, const expr::Placeholders& values) {
    if (!expressions_[index]) {
        return false;
    }
    auto result = expressions_[index]->evaluate(values);
    if (result) {
        return *result;
    }
    const std::string key = config_.conditions[index].name + "|" + result.error().detail;
    if (reported_.insert(key).second) {
        report_("Condition \"" + config_.conditions[index].name + "\" cannot be evaluated: " +
                (result.error().kind == expr::EvalError::Kind::MissingPlaceholder
                     ? "%" + result.error().detail + "% has no value right now"
                     : result.error().detail) +
                ". It counts as false until it can.");
    }
    return false;
}

std::optional<Trigger> Scheduler::poll(const core::TimeInfo& now, const expr::Placeholders& values,
                                       bool updateAvailable) {
    if (config_.dailyRestart.enabled && daily_.due(now)) {
        return Trigger{Trigger::Reason::Daily, "daily restart", config_.dailyRestart.warning, -1, updateAvailable};
    }
    if (now.epoch < suppressedUntil_ || !updateAvailable) {
        resetHolds();
        return std::nullopt;
    }
    if (config_.immediate.enabled) {
        return Trigger{Trigger::Reason::Immediate, "immediate", config_.immediate.warning, -1, true};
    }
    for (std::size_t i = 0; i < config_.conditions.size(); ++i) {
        if (!evaluate(i, values)) {
            holdingSince_[i].reset();
            continue;
        }
        if (!holdingSince_[i]) {
            holdingSince_[i] = now.epoch;
        }
        if (now.epoch - *holdingSince_[i] >= config_.conditions[i].graceSeconds) {
            return Trigger{Trigger::Reason::Condition, config_.conditions[i].name, config_.conditions[i].warning,
                           static_cast<int>(i), true};
        }
    }
    return std::nullopt;
}

bool Scheduler::stillHolds(const Trigger& trigger, const expr::Placeholders& values) {
    if (trigger.reason != Trigger::Reason::Condition || trigger.conditionIndex < 0) {
        return true;
    }
    return evaluate(static_cast<std::size_t>(trigger.conditionIndex), values);
}

} // namespace basalt::hoist::runtime
