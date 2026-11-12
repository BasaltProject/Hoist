#pragma once

#include <basalt/hoist/config/Config.hpp>
#include <basalt/hoist/github/GitHub.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace basalt::hoist::install {

struct Record {
    std::string path; // relative to the working directory
    std::string version;
    std::int64_t publishedAt = 0;
};

struct State {
    std::optional<Record> installed;
    std::vector<Record> previous;   // newest first
    std::optional<Record> rejected; // a version that failed to start, never applied again by itself
    std::string etag;
    std::int64_t lastCheck = 0;
};

std::expected<State, std::string> loadState(const std::filesystem::path& file);
std::expected<void, std::string> saveState(const std::filesystem::path& file, const State& state);

// Everything Hoist keeps for one folder lives under <folder>/.hoist.
class Layout {
public:
    explicit Layout(std::filesystem::path workingDirectory) : root_(std::move(workingDirectory)) {}

    const std::filesystem::path& workingDirectory() const { return root_; }

    std::filesystem::path stateFile() const { return root_ / ".hoist" / "state.toml"; }

    std::filesystem::path binaries() const { return root_ / ".hoist" / "bin"; }

    std::filesystem::path staging() const { return root_ / ".hoist" / "staging"; }

    std::filesystem::path resolve(const Record& record) const { return root_ / record.path; }

private:
    std::filesystem::path root_;
};

std::optional<Record> adoptExisting(const Layout& layout, config::Target target);

// Downloads the asset for this platform and checks it against SHA256SUMS. Nothing unverified is ever left behind.
std::expected<std::filesystem::path, std::string> downloadVerified(const github::GitHubClient& client,
                                                                   const github::Release& release,
                                                                   const github::Asset& asset, const Layout& layout);

std::expected<void, std::string> activate(const Layout& layout, State& state, const std::filesystem::path& staged,
                                          const Record& record, int keepPrevious);

std::expected<Record, std::string> rollback(const Layout& layout, State& state);

std::expected<std::filesystem::path, std::string> backupData(const Layout& layout, const config::BackupConfig& settings,
                                                             std::string_view from, std::string_view to,
                                                             std::int64_t now);

} // namespace basalt::hoist::install
