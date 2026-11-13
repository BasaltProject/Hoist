#include <basalt/hoist/install/Backup.hpp>
#include <basalt/hoist/instance/Observer.hpp>
#include <basalt/hoist/net/Http.hpp>
#include <basalt/hoist/runtime/Commands.hpp>
#include <basalt/hoist/runtime/Lock.hpp>
#include <basalt/hoist/runtime/Log.hpp>
#include <basalt/hoist/runtime/Provisioning.hpp>

#include <chrono>
#include <fstream>
#include <iostream>

namespace basalt::hoist::runtime {
namespace fs = std::filesystem;

namespace {

struct Loaded {
    config::Config config;
    install::Layout layout;
    install::State state;
};

std::int64_t nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

void detectTarget(RunOptions& options) {
    if (options.targetChosen) {
        return;
    }
    const install::Layout layout(fs::absolute(options.directory));
    const fs::path configDirectory =
        options.configDirectory.empty() ? layout.configDirectory() : fs::absolute(options.configDirectory);
    std::error_code ec;
    if (fs::exists(configDirectory / "launcher.toml", ec)) {
        return;
    }
    if (const auto found = install::detectProduct(layout)) {
        options.target = *found;
        options.targetChosen = true;
        logInfo("Found Basalt " + std::string(config::nameOf(*found)) + " in this folder.");
    }
}

namespace {

std::expected<Loaded, int> load(const RunOptions& options) {
    install::Layout layout(fs::absolute(options.directory));
    const fs::path configDirectory =
        options.configDirectory.empty() ? layout.configDirectory() : fs::absolute(options.configDirectory);
    auto config = config::loadConfigDirectory(configDirectory, options.target);
    if (!config) {
        logError("Invalid configuration: " + config.error().describe());
        return std::unexpected(static_cast<int>(kExitBadConfig));
    }
    auto state = install::loadState(layout.stateFile());
    if (!state) {
        logError(state.error());
        return std::unexpected(static_cast<int>(kExitError));
    }
    return Loaded{std::move(*config), std::move(layout), std::move(*state)};
}

std::string when(std::int64_t epoch) {
    if (epoch <= 0) {
        return "never";
    }
    const auto raw = static_cast<std::time_t>(epoch);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &raw);
#else
    localtime_r(&raw, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

core::Installed installedOf(const install::State& state) {
    core::Installed installed;
    if (state.installed) {
        if (auto version = core::parseVersion(state.installed->version)) {
            installed = {*version, state.installed->publishedAt};
        }
    }
    return installed;
}

void leaveRequest(const fs::path& directory, const char* name, const std::string& content = {}) {
    std::error_code ec;
    fs::create_directories(requestsDirectory(directory), ec);
    std::ofstream(requestsDirectory(directory) / name)
        << (content.empty() ? std::to_string(nowEpoch()) : content) << "\n";
}

} // namespace

int commandInit(const RunOptions& options) {
    const fs::path configDirectory = options.configDirectory.empty()
                                         ? install::Layout(fs::absolute(options.directory)).configDirectory()
                                         : fs::absolute(options.configDirectory);
    if (auto written = config::writeMissingDefaults(configDirectory, options.target); !written) {
        logError(written.error().describe());
        return kExitError;
    }
    logInfo("Configuration ready in " + configDirectory.string() + ". Edit it, then run `hoist run`.");
    return kExitOk;
}

int commandCheck(const RunOptions& options) {
    auto loaded = load(options);
    if (!loaded) {
        return loaded.error();
    }
    auto& [config, layout, state] = *loaded;
    if (!state.installed) {
        if (auto adopted = install::adoptExisting(layout, config.launcher.target)) {
            state.installed = adopted;
        }
    }
    const auto http = net::makeNativeHttpClient(config.updates.caFile);
    github::GitHubClient client(*http, repositoryOf(config), config.updates.apiUrl, githubToken(config.updates));
    client.limitDownloads(static_cast<std::size_t>(config.updates.downloadLimitKibPerSecond));
    const std::string product(config::productName(config.launcher.target));
    auto fetched = client.latest(config.updates.channel, product, "");
    if (!fetched) {
        logError("Check failed: " + fetched.error().message);
        return kExitError;
    }
    const auto& release = fetched->release;
    std::cout << "repository:  " << repositoryOf(config) << "\n"
              << "channel:     " << core::nameOf(config.updates.channel) << ", scope "
              << core::nameOf(config.updates.scope) << "\n"
              << "installed:   " << (state.installed ? state.installed->version : "nothing yet") << "\n";
    if (!release.version) {
        std::cout << "latest:      " << release.tag << " (version not readable)\n";
        return kExitError;
    }
    std::cout << "latest:      " << core::toString(*release.version) << " (published " << when(release.publishedAt)
              << ")\n";
    if (!state.installed) {
        std::cout << "result:      nothing installed; `hoist run` installs it\n";
        return kExitOk;
    }
    const core::Candidate candidate{*release.version, release.publishedAt};
    const auto verdict = core::judge(config.updates.channel, config.updates.scope, installedOf(state), candidate);
    const auto jump = core::classify(installedOf(state).version, candidate.version);
    switch (verdict) {
    case core::Verdict::UpToDate:
        std::cout << "result:      up to date\n";
        break;
    case core::Verdict::Allowed:
        std::cout << "result:      update available (" << core::nameOf(jump) << " update, inside the scope)\n";
        break;
    case core::Verdict::OutOfScope:
        std::cout << "result:      " << core::nameOf(jump)
                  << " update available, outside the scope: it will not be installed\n";
        break;
    }
    return kExitOk;
}

int commandStatus(const RunOptions& options) {
    auto loaded = load(options);
    if (!loaded) {
        return loaded.error();
    }
    auto& [config, layout, state] = *loaded;
    const bool running = folderLockHeld(layout.stateFile().parent_path() / "hoist.lock");
    std::cout << "product:     " << config::productName(config.launcher.target)
              << (config.launcher.managed ? "" : " (not managed)") << "\n"
              << "hoist:       " << (running ? "running in this folder" : "not running") << "\n"
              << "installed:   "
              << (state.installed ? state.installed->version + " (" + state.installed->path + ")" : "nothing") << "\n"
              << "channel:     " << core::nameOf(config.updates.channel) << ", scope "
              << core::nameOf(config.updates.scope) << "\n"
              << "last check:  " << when(state.lastCheck) << "\n";
    for (const auto& previous : state.previous) {
        std::cout << "previous:    " << previous.version << "\n";
    }
    if (state.rejected) {
        std::cout << "rejected:    " << state.rejected->version
                  << " (failed to start; not installed again by itself)\n";
    }
    if (running) {
        const auto http = net::makeNativeHttpClient("");
        instance::Observer observer(config.launcher.target, config.status, layout.workingDirectory(), *http,
                                    [](const std::string& message) { logWarn(message); });
        const auto live = observer.query();
        if (live.playersOnline) {
            std::cout << "players:     " << *live.playersOnline << "/" << live.playersMax.value_or(0) << " (via "
                      << (live.source == instance::Status::Source::Api ? "the API" : "the server list ping") << ")\n";
        } else {
            std::cout << "players:     unknown\n";
        }
        if (live.tps) {
            std::cout << "tps:         " << *live.tps << "\n";
        }
    }
    std::cout << "schedule:    daily restart " << (config.schedule.dailyRestart.enabled ? "on" : "off")
              << ", immediate " << (config.schedule.immediate.enabled ? "on" : "off") << ", "
              << config.schedule.conditions.size() << " condition(s)\n";
    return kExitOk;
}

int commandUpdate(const RunOptions& options) {
    auto loaded = load(options);
    if (!loaded) {
        return loaded.error();
    }
    auto& [config, layout, state] = *loaded;
    if (folderLockHeld(layout.stateFile().parent_path() / "hoist.lock")) {
        leaveRequest(layout.workingDirectory(), "update");
        logInfo("Asked the running Hoist to apply the update now.");
        return kExitOk;
    }
    if (auto ready = ensureInstalled(config, layout, state); !ready) {
        logError(ready.error());
        return kExitInstallFailed;
    }
    const auto http = net::makeNativeHttpClient(config.updates.caFile);
    github::GitHubClient client(*http, repositoryOf(config), config.updates.apiUrl, githubToken(config.updates));
    client.limitDownloads(static_cast<std::size_t>(config.updates.downloadLimitKibPerSecond));
    const std::string product(config::productName(config.launcher.target));
    auto fetched = client.latest(config.updates.channel, product, "");
    if (!fetched || !fetched->release.version) {
        logError("Check failed: " + (fetched ? std::string("version not readable") : fetched.error().message));
        return kExitError;
    }
    const auto& release = fetched->release;
    const core::Candidate candidate{*release.version, release.publishedAt};
    const auto verdict = core::judge(config.updates.channel, config.updates.scope, installedOf(state), candidate);
    if (verdict != core::Verdict::Allowed) {
        logInfo(verdict == core::Verdict::UpToDate ? "Already up to date."
                                                   : "The newest version is outside the configured scope.");
        return kExitOk;
    }
    const std::string platform =
        config.updates.assetPlatform.empty() ? github::hostPlatform() : config.updates.assetPlatform;
    const auto* asset = github::selectAsset(release, product, platform, github::hostArch());
    if (!asset) {
        logError("The release has no binary for " + platform + "-" + github::hostArch() + ".");
        return kExitError;
    }
    auto staged = install::downloadVerified(client, release, *asset, layout);
    if (!staged) {
        logError(staged.error());
        return kExitError;
    }
    const auto jump = core::classify(installedOf(state).version, candidate.version);
    if (config.backup.enabled && config.backup.beforeUpdate && core::allows(config.backup.minScope, jump)) {
        auto copy = install::createBackup(
            layout, config.backup, "update-" + state.installed->version + "-to-" + core::toString(candidate.version),
            nowEpoch());
        if (!copy) {
            logError("Backup failed, nothing was changed: " + copy.error());
            return kExitError;
        }
        logInfo("Backup " + copy->name + " written.");
    }
    auto done =
        install::activate(layout, state, *staged, {"", core::toString(candidate.version), candidate.publishedAt},
                          config.updates.keepPrevious);
    if (!done) {
        logError(done.error());
        return kExitError;
    }
    logInfo("Updated to " + core::toString(candidate.version) + ". Restart the product to run it.");
    return kExitOk;
}

int commandRollback(const RunOptions& options) {
    auto loaded = load(options);
    if (!loaded) {
        return loaded.error();
    }
    auto& [config, layout, state] = *loaded;
    if (folderLockHeld(layout.stateFile().parent_path() / "hoist.lock")) {
        leaveRequest(layout.workingDirectory(), "rollback");
        logInfo("Asked the running Hoist to go back to the previous version.");
        return kExitOk;
    }
    auto back = install::rollback(layout, state);
    if (!back) {
        logError(back.error());
        return kExitError;
    }
    logInfo("Went back to " + back->version + ".");
    return kExitOk;
}

int commandBackup(const RunOptions& options) {
    auto loaded = load(options);
    if (!loaded) {
        return loaded.error();
    }
    auto& [config, layout, state] = *loaded;
    const std::string action = options.arguments.empty() ? "list" : options.arguments.front();
    const std::string argument = options.arguments.size() > 1 ? options.arguments[1] : "";
    const fs::path root = install::backupRoot(layout, config.backup);
    const bool running = folderLockHeld(layout.stateFile().parent_path() / "hoist.lock");
    if (action == "list") {
        const auto all = install::listBackups(root);
        if (all.empty()) {
            std::cout << "no backups in " << root.string() << "\n";
        }
        for (const auto& info : all) {
            std::cout << info.name << (info.zipped ? ".zip" : "") << "  " << (info.size >> 20) << " MiB\n";
        }
        return kExitOk;
    }
    if (action == "create") {
        if (config.backup.paths.empty()) {
            logError("backup.toml lists no paths to copy.");
            return kExitError;
        }
        if (running) {
            leaveRequest(layout.workingDirectory(), "backup", argument.empty() ? "manual" : argument);
            logInfo("Asked the running Hoist to take a backup now.");
            return kExitOk;
        }
        auto copy = install::createBackup(layout, config.backup, argument.empty() ? "manual" : argument, nowEpoch());
        if (!copy) {
            logError("Backup failed: " + copy.error());
            return kExitError;
        }
        logInfo("Backup " + copy->name + " written to " + copy->path.string() + " (" + std::to_string(copy->files) +
                " files).");
        return kExitOk;
    }
    if (action == "restore") {
        if (argument.empty()) {
            logError("Usage: hoist backup restore <name|latest>");
            return kExitUsage;
        }
        if (running) {
            if (auto found = install::findBackup(root, argument); !found) {
                logError(found.error());
                return kExitError;
            }
            leaveRequest(layout.workingDirectory(), "restore", argument);
            logInfo("Asked the running Hoist to stop the product, restore " + argument + " and start it again.");
            return kExitOk;
        }
        auto restored = install::restoreBackup(layout, config.backup, argument, nowEpoch());
        if (!restored) {
            logError("Restore failed: " + restored.error());
            return kExitError;
        }
        logInfo("Backup " + restored->name + " restored. A copy of what it replaced was taken first.");
        return kExitOk;
    }
    if (action == "delete") {
        if (argument.empty()) {
            logError("Usage: hoist backup delete <name>");
            return kExitUsage;
        }
        if (auto done = install::deleteBackup(root, argument); !done) {
            logError(done.error());
            return kExitError;
        }
        logInfo("Backup " + argument + " deleted.");
        return kExitOk;
    }
    logError("Unknown backup command: " + action + ". Use create, list, restore or delete.");
    return kExitUsage;
}

} // namespace basalt::hoist::runtime
