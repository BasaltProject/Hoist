#include <basalt/hoist/core/Time.hpp>
#include <basalt/hoist/instance/Observer.hpp>
#include <basalt/hoist/net/Http.hpp>
#include <basalt/hoist/process/Process.hpp>
#include <basalt/hoist/runtime/Lock.hpp>
#include <basalt/hoist/runtime/Log.hpp>
#include <basalt/hoist/runtime/Provisioning.hpp>
#include <basalt/hoist/runtime/Scheduler.hpp>
#include <basalt/hoist/runtime/Supervisor.hpp>
#include <basalt/hoist/runtime/UpdateWorker.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

namespace basalt::hoist::runtime {
namespace fs = std::filesystem;
using Steady = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

std::atomic<int> signalCount{0};

extern "C" void onSignal(int) { signalCount.fetch_add(1); }

std::int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string describe(const process::ExitStatus& status) {
    return status.signaled ? "signal " + std::to_string(status.signal) : "code " + std::to_string(status.code);
}

class Supervisor {
public:
    explicit Supervisor(const RunOptions& options) : options_(options), layout_(fs::absolute(options.directory)) {}

    int run();

private:
    enum class Phase { Running, Preparing, Counting, Waiting };

    bool prepareConfig();
    void startPoller();
    void tick();
    void refreshPlaceholders(const core::TimeInfo& now, const std::optional<PendingUpdate>& pending);
    void handleRequests();
    void reloadConfiguration();
    void beginApply(Trigger trigger, const std::optional<PendingUpdate>& pending);
    void beginCountdown();
    void tickCountdown(const core::TimeInfo& now);
    void executeRestart();
    bool startChild();
    process::ExitStatus stopChild();
    void onChildExit(const process::ExitStatus& status);
    void rollBack(const std::string& why);
    void crashed(const std::string& why);
    void saveState();
    void shutdown(int code);
    void sleepFor(std::chrono::milliseconds time);

    RunOptions options_;
    install::Layout layout_;
    config::Config config_;
    install::State state_;
    std::unique_ptr<FolderLock> lock_;
    std::unique_ptr<UpdateWorker> worker_;
    std::unique_ptr<Scheduler> scheduler_;
    std::unique_ptr<net::HttpClient> observerHttp_;
    std::unique_ptr<instance::Observer> observer_;
    std::jthread poller_;
    std::mutex statusMutex_;
    instance::Status status_;

    std::unique_ptr<process::Process> child_;
    std::jthread inputThread_;
    std::atomic<bool> inputStop_{false};
    std::atomic<bool> childAlive_{false};
    Steady::time_point childStarted_;

    Phase phase_ = Phase::Running;
    Trigger plan_;
    core::Countdown countdown_;
    expr::Placeholders placeholders_;
    Steady::time_point restartAt_;
    std::optional<Steady::time_point> manualUntil_;
    fs::path configDirectory_;
    std::map<std::string, fs::file_time_type> configStamps_;
    Steady::time_point nextConfigCheck_;
    int crashes_ = 0;
    bool probation_ = false;
    Steady::time_point probationUntil_;
    int exitCode_ = kExitOk;
    bool done_ = false;
    std::string lastPrepareError_;
};

void Supervisor::saveState() {
    state_.etag = worker_ ? worker_->etag() : state_.etag;
    state_.lastCheck = worker_ ? worker_->lastCheck() : state_.lastCheck;
    if (auto saved = install::saveState(layout_.stateFile(), state_); !saved) {
        logWarn("Cannot save Hoist's state: " + saved.error());
    }
}

void Supervisor::shutdown(int code) {
    exitCode_ = code;
    done_ = true;
}

void Supervisor::sleepFor(std::chrono::milliseconds time) { std::this_thread::sleep_for(time); }

bool Supervisor::prepareConfig() {
    configDirectory_ = options_.configDirectory.empty() ? layout_.workingDirectory() / "config"
                                                        : fs::absolute(options_.configDirectory);
    auto config = config::loadConfigDirectory(configDirectory_, options_.target);
    if (!config) {
        logError("Invalid configuration: " + config.error().describe());
        exitCode_ = kExitBadConfig;
        return false;
    }
    config_ = std::move(*config);
    for (const auto& warning : config_.warnings) {
        logWarn(warning);
    }
    std::error_code ec;
    for (const char* file : {"launcher.toml", "updates.toml", "schedule.toml", "status.toml"}) {
        configStamps_[file] = fs::last_write_time(configDirectory_ / file, ec);
    }
    return true;
}

void Supervisor::reloadConfiguration() {
    std::error_code ec;
    for (auto& [file, stamp] : configStamps_) {
        const auto current = fs::last_write_time(configDirectory_ / file, ec);
        if (ec || current == stamp) {
            continue;
        }
        stamp = current;
        if (file != "schedule.toml") {
            logWarn(file + " changed; restart Hoist to apply it.");
            continue;
        }
        std::ifstream in(configDirectory_ / file, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        std::vector<std::string> warnings;
        auto schedule = config::parseSchedule(text.str(), warnings);
        if (!schedule) {
            logError("schedule.toml was not reloaded, the previous one stays in force: " + schedule.error().describe());
            continue;
        }
        for (const auto& warning : warnings) {
            logWarn(warning);
        }
        config_.schedule = std::move(*schedule);
        const auto now = core::toTimeInfo(nowEpoch(), config_.schedule.timezone);
        scheduler_ =
            std::make_unique<Scheduler>(config_.schedule, *now, [](const std::string& text) { logWarn(text); });
        logInfo("schedule.toml reloaded.");
    }
}

bool Supervisor::startChild() {
    process::LaunchSpec spec;
    if (config_.launcher.managed) {
        spec.command = fs::absolute(layout_.resolve(*state_.installed)).string();
    } else {
        fs::path command = config_.launcher.command;
        spec.command = (command.has_parent_path() && command.is_relative())
                           ? (layout_.workingDirectory() / command).string()
                           : command.string();
    }
    spec.args = config_.launcher.args;
    spec.workingDirectory = fs::absolute(layout_.workingDirectory() / config_.launcher.workingDirectory);
    auto spawned = process::spawn(spec);
    if (!spawned) {
        logError(spawned.error());
        return false;
    }
    child_ = std::move(*spawned);
    childAlive_ = true;
    childStarted_ = Steady::now();
    inputStop_ = false;
    inputThread_ = std::jthread([this] { process::forwardInput(*child_, inputStop_); });
    logInfo("Started " + fs::path(spec.command).filename().string() + " (pid " + std::to_string(child_->id()) + ").");
    return true;
}

process::ExitStatus Supervisor::stopChild() {
    logInfo("Stopping the product.");
    child_->writeLine(config_.launcher.stopCommand);
    const auto deadline = Steady::now() + std::chrono::seconds(config_.launcher.stopTimeoutSeconds);
    bool forced = false;
    auto lastNotice = Steady::now();
    std::optional<process::ExitStatus> status;
    while (!(status = child_->poll())) {
        const auto now = Steady::now();
        if (!forced && (now > deadline || signalCount.load() >= 2)) {
            if (config_.launcher.killAfterTimeout || signalCount.load() >= 2) {
                logWarn("The product did not stop in time; ending it.");
                child_->terminate();
                forced = true;
                const auto killAt = now + 10s;
                while (!(status = child_->poll()) && Steady::now() < killAt) {
                    sleepFor(100ms);
                }
                if (status) {
                    break;
                }
                child_->kill();
            } else if (now - lastNotice > 30s) {
                lastNotice = now;
                logWarn("Still waiting for the product to stop.");
            }
        }
        sleepFor(100ms);
    }
    inputStop_ = true;
    inputThread_ = {};
    childAlive_ = false;
    child_.reset();
    return *status;
}

void Supervisor::crashed(const std::string& why) {
    const auto ranFor = Steady::now() - childStarted_;
    if (ranFor >= std::chrono::seconds(config_.launcher.stableSeconds)) {
        crashes_ = 0;
    }
    ++crashes_;
    logError(why);
    if (!config_.launcher.restartOnCrash || crashes_ > config_.launcher.maxConsecutiveCrashes) {
        logError("Not restarting: the product keeps failing.");
        shutdown(kExitCrashLoop);
        return;
    }
    restartAt_ = Steady::now() + std::chrono::seconds(config_.launcher.restartDelaySeconds);
    phase_ = Phase::Waiting;
    logInfo("Restarting in " + std::to_string(config_.launcher.restartDelaySeconds) + " seconds (failure " +
            std::to_string(crashes_) + " of " + std::to_string(config_.launcher.maxConsecutiveCrashes) + ").");
}

void Supervisor::rollBack(const std::string& why) {
    probation_ = false;
    auto back = install::rollback(layout_, state_);
    if (!back) {
        crashed(why + " (and no previous version to go back to: " + back.error() + ")");
        return;
    }
    logError(why + " Going back to " + back->version + "; the failed version will not be installed again by itself.");
    if (auto version = core::parseVersion(back->version)) {
        worker_->setInstalled({*version, back->publishedAt}, state_.rejected);
    }
    saveState();
    phase_ = Phase::Running;
    if (!startChild()) {
        crashed("The previous version does not start either.");
    }
}

void Supervisor::onChildExit(const process::ExitStatus& status) {
    inputStop_ = true;
    inputThread_ = {};
    childAlive_ = false;
    child_.reset();
    if (status.clean()) {
        logInfo("The product stopped by itself with exit code 0; Hoist stops too.");
        shutdown(kExitOk);
        return;
    }
    const std::string why = "The product exited with " + describe(status) + ".";
    if (probation_ && Steady::now() < probationUntil_ && !state_.previous.empty()) {
        rollBack(why);
        return;
    }
    crashed(why);
}

void Supervisor::refreshPlaceholders(const core::TimeInfo& now, const std::optional<PendingUpdate>& pending) {
    auto& p = placeholders_;
    p.set("hour", static_cast<double>(now.hour));
    p.set("minute", static_cast<double>(now.minute));
    p.set("time", static_cast<double>(now.minutesOfDay()));
    p.set("weekday", static_cast<double>(now.weekday));
    p.set("day", static_cast<double>(now.day));
    p.set("month", static_cast<double>(now.month));
    p.set("year", static_cast<double>(now.year));
    instance::Status status;
    {
        std::lock_guard lock(statusMutex_);
        status = status_;
    }
    const auto setOptional = [&](const char* name, const auto& value) {
        if (value) {
            p.set(name, static_cast<double>(*value));
        } else {
            p.unset(name);
        }
    };
    setOptional("players_online", status.playersOnline);
    setOptional("players_max", status.playersMax);
    setOptional("tps", status.tps);
    if (child_) {
        p.set("uptime_minutes",
              static_cast<double>(
                  std::chrono::duration_cast<std::chrono::minutes>(Steady::now() - childStarted_).count()));
    } else {
        p.unset("uptime_minutes");
    }
    p.set("channel", std::string(core::nameOf(config_.updates.channel)));
    p.set("update_scope", std::string(core::nameOf(config_.updates.scope)));
    p.set("installed_version", state_.installed ? state_.installed->version : std::string{});
    const bool available = pending && pending->verdict == core::Verdict::Allowed;
    p.set("update_available", available);
    if (available) {
        p.set("new_version", core::toString(pending->candidate.version));
        p.set("update_jump", std::string(core::nameOf(pending->jump)));
    } else {
        p.unset("new_version");
        p.unset("update_jump");
    }
}

void Supervisor::beginApply(Trigger trigger, const std::optional<PendingUpdate>& pending) {
    plan_ = std::move(trigger);
    plan_.applyUpdate = plan_.applyUpdate && pending && pending->verdict == core::Verdict::Allowed;
    logInfo("Trigger \"" + plan_.name +
            "\": " + (plan_.applyUpdate ? "restarting to apply the update." : "restarting."));
    if (plan_.applyUpdate && !(pending && pending->staged)) {
        worker_->prepare();
        phase_ = Phase::Preparing;
        return;
    }
    beginCountdown();
}

void Supervisor::beginCountdown() {
    if (plan_.warning.seconds > 0 && child_) {
        countdown_.begin(nowEpoch(), plan_.warning.seconds);
        phase_ = Phase::Counting;
        return;
    }
    executeRestart();
}

void Supervisor::tickCountdown(const core::TimeInfo& now) {
    if (!scheduler_->stillHolds(plan_, placeholders_)) {
        countdown_.cancel();
        logInfo("The condition \"" + plan_.name + "\" no longer holds; the restart is cancelled.");
        phase_ = Phase::Running;
        return;
    }
    while (auto left = countdown_.nextAnnouncement(now.epoch)) {
        expr::Placeholders values = placeholders_;
        values.set("restart_in", static_cast<double>(*left));
        logInfo("Restart in " + std::to_string(*left) + " seconds.");
        if (!plan_.warning.command.empty() && child_) {
            child_->writeLine(values.format(plan_.warning.command));
        }
    }
    if (countdown_.finished(now.epoch)) {
        executeRestart();
    }
}

void Supervisor::executeRestart() {
    phase_ = Phase::Running;
    auto pending = plan_.applyUpdate ? worker_->pending() : std::nullopt;
    bool apply = pending && pending->verdict == core::Verdict::Allowed && pending->staged;
    if (child_) {
        stopChild();
    }
    if (apply) {
        const std::string from = state_.installed ? state_.installed->version : "unknown";
        const std::string to = core::toString(pending->candidate.version);
        if (config_.updates.backup.enabled && core::allows(config_.updates.backup.minScope, pending->jump)) {
            auto copy = install::backupData(layout_, config_.updates.backup, from, to, nowEpoch());
            if (copy) {
                logInfo("Backup written to " + copy->string() + ".");
            } else {
                logError("Backup failed, the update is not applied: " + copy.error());
                apply = false;
            }
        }
    }
    if (apply) {
        const install::Record record{"", core::toString(pending->candidate.version), pending->candidate.publishedAt};
        auto done = install::activate(layout_, state_, *pending->staged, record, config_.updates.keepPrevious);
        if (done) {
            logInfo("Updated to " + record.version + ".");
            worker_->discardStaged();
            worker_->setInstalled({pending->candidate.version, pending->candidate.publishedAt}, state_.rejected);
            probation_ = true;
            probationUntil_ = Steady::now() + std::chrono::seconds(config_.updates.rollbackGraceSeconds);
            saveState();
        } else {
            logError("Cannot install the update: " + done.error());
        }
    }
    scheduler_->resetHolds();
    if (!startChild()) {
        if (probation_ && !state_.previous.empty()) {
            rollBack("The updated product does not start.");
        } else {
            crashed("The product does not start.");
        }
    }
}

void Supervisor::handleRequests() {
    std::error_code ec;
    const fs::path update = requestsDirectory(layout_.workingDirectory()) / "update";
    const fs::path rollback = requestsDirectory(layout_.workingDirectory()) / "rollback";
    if (phase_ != Phase::Running) {
        return;
    }
    if (fs::exists(update, ec)) {
        fs::remove(update, ec);
        worker_->checkNow();
        manualUntil_ = Steady::now() + 60s;
        logInfo("Update requested; checking for a new version.");
    } else if (fs::exists(rollback, ec)) {
        fs::remove(rollback, ec);
        if (state_.previous.empty()) {
            logWarn("Rollback requested, but there is no previous version.");
        } else {
            if (child_) {
                stopChild();
            }
            rollBack("Rollback requested.");
        }
    }
}

void Supervisor::tick() {
    if (child_) {
        if (auto status = child_->poll()) {
            onChildExit(*status);
            return;
        }
    }
    if (phase_ == Phase::Waiting) {
        if (Steady::now() >= restartAt_) {
            phase_ = Phase::Running;
            if (!startChild()) {
                if (probation_ && !state_.previous.empty()) {
                    rollBack("The updated product does not start.");
                } else {
                    crashed("The product does not start.");
                }
            }
        }
        return;
    }
    const auto now = core::toTimeInfo(nowEpoch(), config_.schedule.timezone);
    if (!now) {
        return;
    }
    auto pending = worker_->pending();
    refreshPlaceholders(*now, pending);
    if (probation_ && child_ && Steady::now() >= probationUntil_) {
        probation_ = false;
        logInfo("The updated version has run long enough; the update is confirmed.");
    }
    if (phase_ == Phase::Running && Steady::now() >= nextConfigCheck_) {
        nextConfigCheck_ = Steady::now() + 2s;
        reloadConfiguration();
    }
    handleRequests();
    switch (phase_) {
    case Phase::Running:
        if (!child_) {
            return;
        }
        if (manualUntil_) {
            if (pending && pending->verdict == core::Verdict::Allowed) {
                manualUntil_.reset();
                beginApply({Trigger::Reason::Manual, "manual request", {}, -1, true}, pending);
                return;
            }
            if (Steady::now() > *manualUntil_) {
                manualUntil_.reset();
                logInfo("Update requested, but there is nothing to apply.");
            }
        }
        if (auto trigger =
                scheduler_->poll(*now, placeholders_, pending && pending->verdict == core::Verdict::Allowed)) {
            beginApply(std::move(*trigger), pending);
        }
        break;
    case Phase::Preparing:
        if (pending && pending->staged) {
            if (scheduler_->stillHolds(plan_, placeholders_)) {
                beginCountdown();
            } else {
                phase_ = Phase::Running;
            }
        } else if (pending && !pending->prepareError.empty()) {
            if (plan_.reason == Trigger::Reason::Daily) {
                plan_.applyUpdate = false;
                beginCountdown();
            } else {
                scheduler_->suppress(now->epoch + 300);
                phase_ = Phase::Running;
                logWarn("The update could not be prepared; it will be tried again in five minutes.");
            }
        }
        break;
    case Phase::Counting:
        tickCountdown(*now);
        break;
    case Phase::Waiting:
        break;
    }
}

void Supervisor::startPoller() {
    observerHttp_ = net::makeNativeHttpClient("");
    observer_ =
        std::make_unique<instance::Observer>(config_.launcher.target, config_.status, layout_.workingDirectory(),
                                             *observerHttp_, [](const std::string& message) { logWarn(message); });
    poller_ = std::jthread([this](std::stop_token stop) {
        while (!stop.stop_requested()) {
            for (int i = 0; i < 100 && !stop.stop_requested(); ++i) {
                std::this_thread::sleep_for(100ms);
            }
            if (stop.stop_requested() || !childAlive_) {
                continue;
            }
            const auto status = observer_->query();
            std::lock_guard lock(statusMutex_);
            status_ = status;
        }
    });
}

int Supervisor::run() {
    if (!prepareConfig()) {
        return exitCode_;
    }
    lock_ = acquireFolderLock(layout_.stateFile().parent_path() / "hoist.lock");
    if (!lock_) {
        logError("Another Hoist is already running in " + layout_.workingDirectory().string() + ".");
        return kExitBusy;
    }
    auto state = install::loadState(layout_.stateFile());
    if (!state) {
        logError(state.error());
        return kExitError;
    }
    state_ = std::move(*state);
    if (config_.launcher.managed) {
        if (auto ready = ensureInstalled(config_, layout_, state_); !ready) {
            logError("Cannot install the product: " + ready.error());
            return kExitInstallFailed;
        }
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::error_code ec;
    fs::create_directories(requestsDirectory(layout_.workingDirectory()), ec);

    worker_ = std::make_unique<UpdateWorker>(config_.launcher.target, config_.updates, layout_);
    core::Installed installed;
    if (state_.installed) {
        if (auto version = core::parseVersion(state_.installed->version)) {
            installed = {*version, state_.installed->publishedAt};
        }
    }
    if (config_.launcher.managed || state_.installed) {
        worker_->start(installed, state_.rejected, state_.etag);
    }
    const auto started = core::toTimeInfo(nowEpoch(), config_.schedule.timezone);
    scheduler_ =
        std::make_unique<Scheduler>(config_.schedule, *started, [](const std::string& text) { logWarn(text); });
    startPoller();
    if (config_.launcher.managed && state_.installed) {
        logInfo("Hoist " + std::string(config::nameOf(config_.launcher.target)) + " folder; installed version " +
                state_.installed->version + ", channel " + std::string(core::nameOf(config_.updates.channel)) +
                ", scope " + std::string(core::nameOf(config_.updates.scope)) + ".");
    }
    if (!startChild()) {
        crashed("The product does not start.");
    }

    bool shuttingDown = false;
    while (!done_) {
        if (signalCount.load() > 0 && !shuttingDown) {
            shuttingDown = true;
            logInfo("Stopping on request.");
            if (child_) {
                stopChild();
            }
            shutdown(kExitOk);
            break;
        }
        tick();
        sleepFor(250ms);
    }
    poller_.request_stop();
    saveState();
    return exitCode_;
}

} // namespace

fs::path requestsDirectory(const fs::path& directory) { return directory / ".hoist" / "requests"; }

int runSupervisor(const RunOptions& options) { return Supervisor(options).run(); }

} // namespace basalt::hoist::runtime
