#include <basalt/hoist/net/Http.hpp>
#include <basalt/hoist/runtime/Log.hpp>
#include <basalt/hoist/runtime/UpdateWorker.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace basalt::hoist::runtime {
namespace {

std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string keyOf(const github::Release& release) {
    return release.version ? core::toString(*release.version) + "@" + std::to_string(release.publishedAt) : release.tag;
}

} // namespace

UpdateWorker::UpdateWorker(config::Target target, config::UpdatesConfig settings, install::Layout layout)
    : settings_(std::move(settings)), layout_(std::move(layout)), product_(std::string(config::productName(target))),
      repository_(settings_.repository.empty() ? config::defaultRepository(target) : settings_.repository) {
    token_ = settings_.token;
    if (token_.empty()) {
        if (const char* fromEnvironment = std::getenv("HOIST_GITHUB_TOKEN")) {
            token_ = fromEnvironment;
        }
    }
}

UpdateWorker::~UpdateWorker() {
    thread_.request_stop();
    wake_.notify_all();
}

void UpdateWorker::start(core::Installed installed, std::optional<install::Record> rejected, std::string etag) {
    {
        std::lock_guard lock(mutex_);
        installed_ = installed;
        rejected_ = std::move(rejected);
        etag_ = std::move(etag);
        nextCheck_ = 0;
    }
    thread_ = std::jthread([this](std::stop_token stop) { loop(stop); });
}

void UpdateWorker::setInstalled(core::Installed installed, std::optional<install::Record> rejected) {
    std::lock_guard lock(mutex_);
    installed_ = installed;
    rejected_ = std::move(rejected);
}

std::optional<PendingUpdate> UpdateWorker::judged() const {
    if (!latest_ || !latest_->version) {
        return std::nullopt;
    }
    PendingUpdate pending;
    pending.release = *latest_;
    pending.candidate = {*latest_->version, latest_->publishedAt};
    pending.verdict = core::judge(settings_.channel, settings_.scope, installed_, pending.candidate);
    pending.jump = core::classify(installed_.version, pending.candidate.version);
    if (rejected_ && rejected_->version == core::toString(pending.candidate.version) &&
        pending.candidate.publishedAt <= rejected_->publishedAt) {
        pending.verdict = core::Verdict::UpToDate;
    }
    if (staged_ && stagedFor_ == keyOf(*latest_)) {
        pending.staged = staged_;
    }
    pending.prepareError = prepareError_;
    return pending;
}

std::optional<PendingUpdate> UpdateWorker::pending() const {
    std::lock_guard lock(mutex_);
    return judged();
}

void UpdateWorker::checkNow() {
    {
        std::lock_guard lock(mutex_);
        checkRequested_ = true;
    }
    wake_.notify_all();
}

void UpdateWorker::prepare() {
    {
        std::lock_guard lock(mutex_);
        prepareRequested_ = true;
        prepareError_.clear();
    }
    wake_.notify_all();
}

void UpdateWorker::discardStaged() {
    std::lock_guard lock(mutex_);
    staged_.reset();
    stagedFor_.clear();
}

std::string UpdateWorker::etag() const {
    std::lock_guard lock(mutex_);
    return etag_;
}

std::int64_t UpdateWorker::lastCheck() const {
    std::lock_guard lock(mutex_);
    return lastCheck_;
}

void UpdateWorker::loop(std::stop_token stop) {
    while (!stop.stop_requested()) {
        bool doCheck = false;
        bool doDownload = false;
        {
            std::unique_lock lock(mutex_);
            const auto deadline =
                std::chrono::system_clock::time_point(std::chrono::seconds(std::max<std::int64_t>(nextCheck_, 0)));
            wake_.wait_until(lock, stop, deadline, [&] { return checkRequested_ || prepareRequested_; });
            if (stop.stop_requested()) {
                return;
            }
            doCheck = checkRequested_ || now() >= nextCheck_;
            doDownload = prepareRequested_;
            checkRequested_ = false;
            prepareRequested_ = false;
        }
        if (doCheck) {
            check();
        }
        if (doDownload) {
            download();
        }
    }
}

void UpdateWorker::check() {
    static const auto http = net::makeNativeHttpClient(settings_.caFile);
    github::GitHubClient client(*http, repository_, settings_.apiUrl, token_);
    client.limitDownloads(static_cast<std::size_t>(settings_.downloadLimitKibPerSecond));
    std::string etag;
    {
        std::lock_guard lock(mutex_);
        etag = latest_ ? etag_ : std::string{};
    }
    auto fetched = client.latest(settings_.channel, product_, etag);
    std::lock_guard lock(mutex_);
    lastCheck_ = now();
    if (!fetched) {
        ++failures_;
        const int backoff = std::min(failures_, 6);
        nextCheck_ = now() + std::max<std::int64_t>(fetched.error().retryAfterSeconds,
                                                    std::int64_t{settings_.checkIntervalMinutes} * 60 * backoff);
        if (lastFailure_ != fetched.error().message) {
            lastFailure_ = fetched.error().message;
            logWarn("Update check failed: " + fetched.error().message +
                    ". Hoist keeps running the product and retries.");
        }
        return;
    }
    failures_ = 0;
    lastFailure_.clear();
    nextCheck_ = now() + std::int64_t{settings_.checkIntervalMinutes} * 60;
    if (fetched->modified) {
        latest_ = std::move(fetched->release);
        etag_ = fetched->etag;
    }
    const auto pending = judged();
    if (!pending) {
        return;
    }
    const std::string key = keyOf(pending->release) + std::to_string(static_cast<int>(pending->verdict));
    if (key == announced_) {
        return;
    }
    announced_ = key;
    const std::string version = core::toString(pending->candidate.version);
    if (pending->verdict == core::Verdict::Allowed) {
        logInfo("Update available: " + version + " (" + std::string(core::nameOf(pending->jump)) + " update, channel " +
                std::string(core::nameOf(settings_.channel)) + ").");
    } else if (pending->verdict == core::Verdict::OutOfScope) {
        logInfo("Version " + version + " is available but it is a " + std::string(core::nameOf(pending->jump)) +
                " update and the scope is \"" + std::string(core::nameOf(settings_.scope)) +
                "\"; it will not be installed.");
    }
}

void UpdateWorker::download() {
    static const auto http = net::makeNativeHttpClient(settings_.caFile);
    github::Release release;
    {
        std::lock_guard lock(mutex_);
        const auto current = judged();
        if (!current || current->verdict != core::Verdict::Allowed) {
            return;
        }
        if (current->staged) {
            return;
        }
        release = current->release;
    }
    const std::string platform = settings_.assetPlatform.empty() ? github::hostPlatform() : settings_.assetPlatform;
    const auto* asset = github::selectAsset(release, product_, platform, github::hostArch());
    if (!asset) {
        std::lock_guard lock(mutex_);
        prepareError_ = "the release has no " + product_ + " binary for " + platform + "-" + github::hostArch();
        logError("Cannot prepare the update: " + prepareError_ + ".");
        return;
    }
    logInfo("Downloading " + asset->name + ".");
    github::GitHubClient client(*http, repository_, settings_.apiUrl, token_);
    client.limitDownloads(static_cast<std::size_t>(settings_.downloadLimitKibPerSecond));
    auto staged = install::downloadVerified(client, release, *asset, layout_);
    std::lock_guard lock(mutex_);
    if (!staged) {
        prepareError_ = staged.error();
        logError("Cannot prepare the update: " + staged.error() + ".");
        return;
    }
    staged_ = *staged;
    stagedFor_ = keyOf(release);
    logInfo("Update downloaded and verified.");
}

} // namespace basalt::hoist::runtime
