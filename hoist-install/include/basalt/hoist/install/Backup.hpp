#pragma once

#include <basalt/hoist/config/Config.hpp>
#include <basalt/hoist/install/Install.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace basalt::hoist::install {

struct BackupInfo {
    std::string name; // 20261113-040000-scheduled, without .zip
    std::string kind;
    std::int64_t created = 0;
    std::uintmax_t size = 0;
    bool zipped = false;
    std::filesystem::path path;
    std::size_t files = 0;
    std::size_t skipped = 0;
};

std::filesystem::path backupRoot(const Layout& layout, const config::BackupConfig& settings);

// Newest first. Copies that are still being written are not listed.
std::vector<BackupInfo> listBackups(const std::filesystem::path& root);

std::expected<BackupInfo, std::string> createBackup(const Layout& layout, const config::BackupConfig& settings,
                                                    std::string_view kind, std::int64_t now, std::stop_token stop = {});

// Names to delete so that the copies left follow the retention rules. The newest copy always stays.
std::vector<std::string> backupsToPrune(const std::vector<BackupInfo>& newestFirst,
                                        const config::BackupRetention& retention, std::int64_t now);

std::expected<BackupInfo, std::string> findBackup(const std::filesystem::path& root, std::string_view name);

// Puts a copy back over the working folder. The product must be stopped. A safety copy is taken first.
std::expected<BackupInfo, std::string> restoreBackup(const Layout& layout, const config::BackupConfig& settings,
                                                     std::string_view name, std::int64_t now);

std::expected<void, std::string> deleteBackup(const std::filesystem::path& root, std::string_view name);

bool matchesExclude(std::string_view relative, const std::vector<std::string>& patterns);

} // namespace basalt::hoist::install
