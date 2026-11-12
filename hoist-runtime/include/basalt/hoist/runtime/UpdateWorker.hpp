#pragma once

#include <basalt/hoist/config/Config.hpp>
#include <basalt/hoist/core/Version.hpp>
#include <basalt/hoist/github/GitHub.hpp>
#include <basalt/hoist/install/Install.hpp>

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace basalt::hoist::runtime {

struct PendingUpdate {
    github::Release release;
    core::Candidate candidate;
    core::Jump jump = core::Jump::None;
    core::Verdict verdict = core::Verdict::UpToDate;
    std::optional<std::filesystem::path> staged;
    std::string prepareError;
};

class UpdateWorker {
public:
    UpdateWorker(config::Target target, config::UpdatesConfig settings, install::Layout layout);
    ~UpdateWorker();
    UpdateWorker(const UpdateWorker&) = delete;
    UpdateWorker& operator=(const UpdateWorker&) = delete;

    void start(core::Installed installed, std::optional<install::Record> rejected, std::string etag);
    void setInstalled(core::Installed installed, std::optional<install::Record> rejected);

    std::optional<PendingUpdate> pending() const;
    void checkNow();
    void prepare();
    void discardStaged();
    std::string etag() const;
    std::int64_t lastCheck() const;

private:
    void loop(std::stop_token stop);
    void check();
    void download();
    std::optional<PendingUpdate> judged() const;

    config::UpdatesConfig settings_;
    install::Layout layout_;
    std::string product_;
    std::string repository_;
    std::string token_;

    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    core::Installed installed_;
    std::optional<install::Record> rejected_;
    std::optional<github::Release> latest_;
    std::optional<std::filesystem::path> staged_;
    std::string stagedFor_;
    std::string prepareError_;
    std::string etag_;
    std::int64_t lastCheck_ = 0;
    std::int64_t nextCheck_ = 0;
    int failures_ = 0;
    bool checkRequested_ = false;
    bool prepareRequested_ = false;
    std::string announced_;
    std::string lastFailure_;
    std::jthread thread_;
};

} // namespace basalt::hoist::runtime
