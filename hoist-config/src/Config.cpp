#include "Reader.hpp"

#include <basalt/hoist/config/Config.hpp>
#include <basalt/hoist/core/Time.hpp>
#include <basalt/hoist/expr/Expression.hpp>

#include <fstream>
#include <sstream>

namespace basalt::hoist::config {
namespace {

constexpr int kDay = 86400;

template <typename T, typename Fill>
std::expected<T, ConfigError> load(std::string_view text, std::string_view file, std::vector<std::string>& warnings,
                                   Fill fill) {
    auto table = parseToml(text, std::string(file));
    if (!table) {
        return std::unexpected(table.error());
    }
    ConfigError error;
    Reader root(*table, std::string(file), {}, error, warnings);
    if (root.integer("format", 1, 1, 1000000) > kFormat) {
        root.fail("format", "was written by a newer Hoist; this one reads format " + std::to_string(kFormat));
    }
    T value;
    fill(root, value);
    root.finish();
    if (!error.message.empty()) {
        return std::unexpected(error);
    }
    return value;
}

template <typename E>
E choice(const Reader& reader, std::string_view key, E fallback, std::optional<E> (*parse)(std::string_view),
         std::string_view allowed) {
    auto value = reader.text(key, std::string(nameOf(fallback)));
    if (auto parsed = parse(value)) {
        return *parsed;
    }
    reader.fail(key, "must be " + std::string(allowed));
    return fallback;
}

std::optional<Target> parseTarget(std::string_view text) {
    if (text == "server") {
        return Target::Server;
    }
    if (text == "proxy") {
        return Target::Proxy;
    }
    return std::nullopt;
}

Warning readWarning(const Reader& reader) {
    return {reader.integer("warning-seconds", 0, 0, 3600), reader.text("warning-command", "")};
}

} // namespace

std::string_view nameOf(Target target) { return target == Target::Server ? "server" : "proxy"; }

std::string_view productName(Target target) { return target == Target::Server ? "BasaltServer" : "BasaltProxy"; }

std::string defaultRepository(Target target) {
    return target == Target::Server ? "BasaltProject/Server" : "BasaltProject/Proxy";
}

std::string ConfigError::describe() const { return file + (key.empty() ? "" : ": '" + key + "'") + ": " + message; }

std::expected<LauncherConfig, ConfigError> parseLauncher(std::string_view text, std::vector<std::string>& warnings) {
    return load<LauncherConfig>(text, "launcher.toml", warnings, [](const Reader& r, LauncherConfig& c) {
        c.target = choice<Target>(r, "target", c.target, parseTarget, "server or proxy");
        c.managed = r.flag("managed", c.managed);
        c.command = r.text("command", c.command);
        c.args = r.list("args");
        c.workingDirectory = r.text("working-directory", c.workingDirectory);
        c.stopCommand = r.text("stop-command", c.stopCommand);
        c.stopTimeoutSeconds = r.integer("stop-timeout-seconds", c.stopTimeoutSeconds, 1, 3600);
        c.killAfterTimeout = r.flag("kill-after-timeout", c.killAfterTimeout);
        c.restartOnCrash = r.flag("restart-on-crash", c.restartOnCrash);
        c.restartDelaySeconds = r.integer("restart-delay-seconds", c.restartDelaySeconds, 0, 3600);
        c.maxConsecutiveCrashes = r.integer("max-consecutive-crashes", c.maxConsecutiveCrashes, 1, 1000);
        c.stableSeconds = r.integer("stable-seconds", c.stableSeconds, 1, 86400);
        if (!c.managed && c.command.empty()) {
            r.fail("command", "is required when managed is false");
        }
    });
}

std::expected<UpdatesConfig, ConfigError> parseUpdates(std::string_view text, std::vector<std::string>& warnings) {
    return load<UpdatesConfig>(text, "updates.toml", warnings, [](const Reader& r, UpdatesConfig& c) {
        c.channel = choice<core::Channel>(r, "channel", c.channel, core::parseChannel, "release or nightly");
        c.scope = choice<core::Scope>(r, "scope", c.scope, core::parseScope, "build, minor or major");
        c.checkIntervalMinutes = r.integer("check-interval-minutes", c.checkIntervalMinutes, 5, 10080);
        c.repository = r.text("repository", c.repository);
        c.apiUrl = r.text("api-url", c.apiUrl);
        c.token = r.text("token", c.token);
        c.assetPlatform = r.text("asset-platform", c.assetPlatform);
        c.caFile = r.text("ca-file", c.caFile);
        c.predownload = r.flag("predownload", c.predownload);
        c.downloadLimitKibPerSecond =
            r.integer("download-limit-kib-per-second", c.downloadLimitKibPerSecond, 0, 10000000);
        c.keepPrevious = r.integer("keep-previous", c.keepPrevious, 1, 50);
        c.rollbackGraceSeconds = r.integer("rollback-grace-seconds", c.rollbackGraceSeconds, 10, 3600);
    });
}

std::expected<BackupConfig, ConfigError> parseBackup(std::string_view text, std::vector<std::string>& warnings) {
    return load<BackupConfig>(text, "backup.toml", warnings, [](const Reader& r, BackupConfig& c) {
        c.enabled = r.flag("enabled", c.enabled);
        c.destination = r.text("destination", c.destination);
        c.paths = r.list("paths");
        c.exclude = r.list("exclude");
        c.compress = r.flag("compress", c.compress);
        if (c.destination.empty()) {
            r.fail("destination", "must not be empty");
        }
        for (const auto& path : c.paths) {
            const std::filesystem::path item(path);
            bool escapes = item.is_absolute() || item.empty();
            for (const auto& part : item) {
                escapes = escapes || part == "..";
            }
            if (escapes) {
                r.fail("paths", "'" + path + "' must be inside this folder");
            }
        }
        if (auto retention = r.section("retention")) {
            c.retention.keepLast = retention->integer("keep-last", c.retention.keepLast, 1, 1000);
            c.retention.keepDaily = retention->integer("keep-daily", c.retention.keepDaily, 0, 3650);
            c.retention.keepWeekly = retention->integer("keep-weekly", c.retention.keepWeekly, 0, 520);
            c.retention.maxAgeDays = retention->integer("max-age-days", c.retention.maxAgeDays, 0, 36500);
            c.retention.maxTotalMib = retention->integer("max-total-mib", c.retention.maxTotalMib, 0, 100000000);
            retention->finish();
        }
        if (auto before = r.section("before-update")) {
            c.beforeUpdate = before->flag("enabled", c.beforeUpdate);
            c.minScope =
                choice<core::Scope>(*before, "min-scope", c.minScope, core::parseScope, "build, minor or major");
            before->finish();
        }
        if (auto schedule = r.section("schedule")) {
            c.schedule.everyHours = schedule->integer("every-hours", c.schedule.everyHours, 0, 8760);
            const std::string at = schedule->text("daily-at", "");
            if (!at.empty()) {
                c.schedule.daily = core::parseClock(at, c.schedule.hour, c.schedule.minute);
                if (!c.schedule.daily) {
                    schedule->fail("daily-at", "must look like HH:MM");
                }
            }
            c.schedule.when = schedule->text("when", "");
            if (!c.schedule.when.empty()) {
                if (auto parsed = expr::Expression::parse(c.schedule.when); !parsed) {
                    schedule->fail("when", parsed.error().message + " (column " +
                                               std::to_string(parsed.error().column + 1) + ")");
                }
            }
            schedule->finish();
        }
        if (auto commands = r.section("commands")) {
            c.beforeCommands = commands->list("before");
            c.afterCommands = commands->list("after");
            c.settleSeconds = commands->integer("settle-seconds", c.settleSeconds, 0, 600);
            commands->finish();
        }
        const bool automatic = c.beforeUpdate || c.schedule.everyHours > 0 || c.schedule.daily;
        if (c.enabled && automatic && c.paths.empty()) {
            r.fail("paths", "must list what to copy when backups are enabled");
        }
    });
}

std::expected<StatusConfig, ConfigError> parseStatus(std::string_view text, std::vector<std::string>& warnings) {
    return load<StatusConfig>(text, "status.toml", warnings, [](const Reader& r, StatusConfig& c) {
        c.timeoutSeconds = r.integer("timeout-seconds", c.timeoutSeconds, 1, 60);
        if (auto api = r.section("api")) {
            c.api.enabled = api->flag("enabled", c.api.enabled);
            c.api.host = api->text("host", c.api.host);
            c.api.port = api->integer("port", c.api.port, 0, 65535);
            c.api.tokenFile = api->text("token-file", c.api.tokenFile);
            api->finish();
        }
        if (auto ping = r.section("ping")) {
            c.ping.enabled = ping->flag("enabled", c.ping.enabled);
            c.ping.host = ping->text("host", c.ping.host);
            c.ping.port = ping->integer("port", c.ping.port, 1, 65535);
            ping->finish();
        }
    });
}

std::expected<ScheduleConfig, ConfigError> parseSchedule(std::string_view text, std::vector<std::string>& warnings) {
    return load<ScheduleConfig>(text, "schedule.toml", warnings, [](const Reader& r, ScheduleConfig& c) {
        c.timezone = r.text("timezone", c.timezone);
        if (!core::isKnownZone(c.timezone)) {
            r.fail("timezone", "must be local, UTC or an IANA zone name this host knows");
        }
        if (auto daily = r.section("daily-restart")) {
            c.dailyRestart.enabled = daily->flag("enabled", false);
            if (!core::parseClock(daily->text("time", "04:00"), c.dailyRestart.hour, c.dailyRestart.minute)) {
                daily->fail("time", "must look like HH:MM");
            }
            c.dailyRestart.warning = readWarning(*daily);
            daily->finish();
        }
        if (auto immediate = r.section("immediate")) {
            c.immediate.enabled = immediate->flag("enabled", false);
            c.immediate.warning = readWarning(*immediate);
            immediate->finish();
        }
        int index = 0;
        for (const auto& entry : r.tables("conditions")) {
            Condition condition;
            condition.name = entry.text("name", "condition " + std::to_string(++index));
            condition.when = entry.text("when", "");
            condition.graceSeconds = entry.integer("grace-seconds", 0, 0, kDay);
            condition.warning = readWarning(entry);
            if (auto parsed = expr::Expression::parse(condition.when); !parsed) {
                entry.fail("when",
                           parsed.error().message + " (column " + std::to_string(parsed.error().column + 1) + ")");
            }
            entry.finish();
            c.conditions.push_back(std::move(condition));
        }
    });
}

std::expected<void, ConfigError> writeMissingDefaults(const std::filesystem::path& directory, Target target) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return std::unexpected(ConfigError{directory.string(), {}, "cannot create the config folder: " + ec.message()});
    }
    for (const auto& [file, content] : defaultFiles(target)) {
        const auto path = directory / file;
        if (std::filesystem::exists(path)) {
            continue;
        }
        std::ofstream out(path, std::ios::binary);
        out << content;
        if (!out) {
            return std::unexpected(ConfigError{path.string(), {}, "cannot write the default file"});
        }
    }
    return {};
}

std::expected<Config, ConfigError> loadConfigDirectory(const std::filesystem::path& directory, Target target) {
    if (auto written = writeMissingDefaults(directory, target); !written) {
        return std::unexpected(written.error());
    }
    const auto read = [&](const char* file) {
        std::ifstream in(directory / file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    };
    Config config;
    auto launcher = parseLauncher(read("launcher.toml"), config.warnings);
    if (!launcher) {
        return std::unexpected(launcher.error());
    }
    auto updates = parseUpdates(read("updates.toml"), config.warnings);
    if (!updates) {
        return std::unexpected(updates.error());
    }
    auto schedule = parseSchedule(read("schedule.toml"), config.warnings);
    if (!schedule) {
        return std::unexpected(schedule.error());
    }
    auto status = parseStatus(read("status.toml"), config.warnings);
    if (!status) {
        return std::unexpected(status.error());
    }
    auto backup = parseBackup(read("backup.toml"), config.warnings);
    if (!backup) {
        return std::unexpected(backup.error());
    }
    config.backup = std::move(*backup);
    config.status = std::move(*status);
    config.launcher = std::move(*launcher);
    config.updates = std::move(*updates);
    config.schedule = std::move(*schedule);
    return config;
}

} // namespace basalt::hoist::config
