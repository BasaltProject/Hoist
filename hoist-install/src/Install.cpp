#include <basalt/hoist/install/Install.hpp>
#include <basalt/hoist/net/Hash.hpp>

#include <algorithm>

namespace basalt::hoist::install {
namespace fs = std::filesystem;

namespace {

void removeQuietly(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
}

bool managedByHoist(const Layout& layout, const Record& record) {
    const auto relative = fs::relative(layout.resolve(record), layout.binaries());
    return !relative.empty() && *relative.begin() != "..";
}

} // namespace

std::optional<Record> adoptExisting(const Layout& layout, config::Target target) {
    const std::string prefix = std::string(config::productName(target)) + "-";
    const std::string suffix = "-" + github::hostPlatform() + "-" + github::hostArch();
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(layout.workingDirectory(), ec)) {
        std::string name = entry.path().filename().string();
        std::string bare = name.ends_with(".exe") ? name.substr(0, name.size() - 4) : name;
        if (!entry.is_regular_file() || !bare.starts_with(prefix) || !bare.ends_with(suffix)) {
            continue;
        }
        const auto version =
            core::parseVersion(bare.substr(prefix.size(), bare.size() - prefix.size() - suffix.size()));
        if (version) {
            return Record{name, core::toString(*version), 0};
        }
    }
    return std::nullopt;
}

std::expected<fs::path, std::string> downloadVerified(const github::GitHubClient& client,
                                                      const github::Release& release, const github::Asset& asset,
                                                      const Layout& layout) {
    const auto* sumsAsset = github::findAsset(release, "SHA256SUMS");
    if (!sumsAsset) {
        return std::unexpected("the release has no SHA256SUMS, so nothing can be verified");
    }
    auto sums = client.fetchText(*sumsAsset);
    if (!sums) {
        return std::unexpected(sums.error());
    }
    const auto expected = github::checksumFor(*sums, asset.name);
    if (!expected) {
        return std::unexpected("SHA256SUMS does not list " + asset.name);
    }
    std::error_code ec;
    fs::create_directories(layout.staging(), ec);
    fs::path target = layout.staging() / asset.name;
    const fs::path partial = layout.staging() / (asset.name + ".part");
    auto response = net::httpDownload(*client.http(), client.downloadRequest(asset), partial, true);
    if (!response) {
        // A dropped connection keeps what arrived so the next attempt can continue it.
        if (response.error().find("ignored the request to resume") != std::string::npos) {
            removeQuietly(partial);
        }
        return std::unexpected("downloading " + asset.name + " failed: " + response.error());
    }
    if (response->status != 200 && response->status != 206) {
        removeQuietly(partial);
        return std::unexpected("downloading " + asset.name + " answered " + std::to_string(response->status));
    }
    auto actual = net::sha256File(partial);
    if (!actual || *actual != *expected) {
        removeQuietly(partial);
        return std::unexpected("the download of " + asset.name + " does not match its SHA-256 and was discarded");
    }
    fs::rename(partial, target, ec);
    if (ec) {
        removeQuietly(partial);
        return std::unexpected("cannot move " + asset.name + " into place: " + ec.message());
    }
    fs::permissions(target, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    return target;
}

std::expected<void, std::string> activate(const Layout& layout, State& state, const fs::path& staged,
                                          const Record& record, int keepPrevious) {
    std::error_code ec;
    fs::create_directories(layout.binaries(), ec);
    const fs::path destination = layout.binaries() / staged.filename();
    fs::rename(staged, destination, ec);
    if (ec) {
        return std::unexpected("cannot move the new version into place: " + ec.message());
    }
    if (state.installed) {
        state.previous.insert(state.previous.begin(), *state.installed);
    }
    Record installed = record;
    installed.path = fs::relative(destination, layout.workingDirectory()).generic_string();
    state.installed = installed;
    while (state.previous.size() > static_cast<std::size_t>(keepPrevious)) {
        if (managedByHoist(layout, state.previous.back())) {
            removeQuietly(layout.resolve(state.previous.back()));
        }
        state.previous.pop_back();
    }
    return saveState(layout.stateFile(), state);
}

std::expected<Record, std::string> rollback(const Layout& layout, State& state) {
    if (state.previous.empty()) {
        return std::unexpected("there is no previous version to go back to");
    }
    std::error_code ec;
    while (!state.previous.empty() && !fs::exists(layout.resolve(state.previous.front()), ec)) {
        state.previous.erase(state.previous.begin());
    }
    if (state.previous.empty()) {
        return std::unexpected("the previous versions are gone from disk");
    }
    if (state.installed) {
        state.rejected = state.installed;
        if (managedByHoist(layout, *state.installed)) {
            removeQuietly(layout.resolve(*state.installed));
        }
    }
    state.installed = state.previous.front();
    state.previous.erase(state.previous.begin());
    if (auto saved = saveState(layout.stateFile(), state); !saved) {
        return std::unexpected(saved.error());
    }
    return *state.installed;
}

} // namespace basalt::hoist::install
