#pragma once

#include <basalt/hoist/core/Version.hpp>

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace basalt::hoist::config {

inline constexpr int kFormat = 1;

enum class Target { Server, Proxy };

std::string_view nameOf(Target target);
std::string_view productName(Target target);
std::string defaultRepository(Target target);

struct ConfigError {
    std::string file;
    std::string key;
    std::string message;

    std::string describe() const;
};

struct LauncherConfig {
    Target target = Target::Server;
    bool managed = true;
    std::string command;
    std::vector<std::string> args;
    std::string workingDirectory = ".";
    std::string stopCommand = "stop";
    int stopTimeoutSeconds = 60;
    bool killAfterTimeout = true;
    bool restartOnCrash = true;
    int restartDelaySeconds = 5;
    int maxConsecutiveCrashes = 5;
    int stableSeconds = 300;
};

struct BackupConfig {
    bool enabled = false;
    core::Scope minScope = core::Scope::Build;
    std::vector<std::string> paths;
    std::string destination = ".hoist/backups";
    int keep = 3;
};

struct ApiConfig {
    bool enabled = true;
    std::string host = "127.0.0.1";
    int port = 0;          // 0 means the product's own default
    std::string tokenFile; // empty means the product's own default
};

struct PingConfig {
    bool enabled = true;
    std::string host = "127.0.0.1";
    int port = 25565;
};

struct StatusConfig {
    ApiConfig api;
    PingConfig ping;
    int timeoutSeconds = 5;
};

struct UpdatesConfig {
    core::Channel channel = core::Channel::Release;
    core::Scope scope = core::Scope::Build;
    int checkIntervalMinutes = 30;
    std::string repository;
    std::string apiUrl = "https://api.github.com";
    std::string token;
    std::string assetPlatform;
    std::string caFile;
    bool predownload = false;
    int downloadLimitKibPerSecond = 0;
    int keepPrevious = 2;
    int rollbackGraceSeconds = 120;
    BackupConfig backup;
};

struct Warning {
    int seconds = 0;
    std::string command;
};

struct Condition {
    std::string name;
    std::string when;
    int graceSeconds = 0;
    Warning warning;
};

struct DailyRestartConfig {
    bool enabled = false;
    int hour = 4;
    int minute = 0;
    Warning warning;
};

struct ImmediateConfig {
    bool enabled = false;
    Warning warning;
};

struct ScheduleConfig {
    std::string timezone = "local";
    DailyRestartConfig dailyRestart;
    ImmediateConfig immediate;
    std::vector<Condition> conditions;
};

struct Config {
    LauncherConfig launcher;
    UpdatesConfig updates;
    ScheduleConfig schedule;
    StatusConfig status;
    std::vector<std::string> warnings;
};

std::expected<Config, ConfigError> loadConfigDirectory(const std::filesystem::path& directory, Target target);
std::vector<std::pair<std::string, std::string>> defaultFiles(Target target);
std::expected<void, ConfigError> writeMissingDefaults(const std::filesystem::path& directory, Target target);

std::expected<LauncherConfig, ConfigError> parseLauncher(std::string_view text, std::vector<std::string>& warnings);
std::expected<UpdatesConfig, ConfigError> parseUpdates(std::string_view text, std::vector<std::string>& warnings);
std::expected<ScheduleConfig, ConfigError> parseSchedule(std::string_view text, std::vector<std::string>& warnings);
std::expected<StatusConfig, ConfigError> parseStatus(std::string_view text, std::vector<std::string>& warnings);

} // namespace basalt::hoist::config
