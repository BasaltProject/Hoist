#include <basalt/hoist/net/Http.hpp>
#include <basalt/hoist/runtime/Log.hpp>
#include <basalt/hoist/runtime/Provisioning.hpp>

#include <cstdlib>

namespace basalt::hoist::runtime {

std::string githubToken(const config::UpdatesConfig& updates) {
    if (!updates.token.empty()) {
        return updates.token;
    }
    const char* fromEnvironment = std::getenv("HOIST_GITHUB_TOKEN");
    return fromEnvironment ? fromEnvironment : "";
}

std::string repositoryOf(const config::Config& config) {
    return config.updates.repository.empty() ? config::defaultRepository(config.launcher.target)
                                             : config.updates.repository;
}

std::expected<void, std::string> ensureInstalled(const config::Config& config, const install::Layout& layout,
                                                 install::State& state) {
    std::error_code ec;
    if (state.installed && std::filesystem::exists(layout.resolve(*state.installed), ec)) {
        return {};
    }
    const auto target = config.launcher.target;
    if (auto adopted = install::adoptExisting(layout, target)) {
        logInfo("Using the " + std::string(config::productName(target)) +
                " binary found in this folder: " + adopted->path + ".");
        state.installed = *adopted;
        return install::saveState(layout.stateFile(), state);
    }
    logInfo("No " + std::string(config::productName(target)) + " binary yet; installing the latest " +
            std::string(core::nameOf(config.updates.channel)) + ".");
    const auto http = net::makeNativeHttpClient(config.updates.caFile);
    github::GitHubClient client(*http, repositoryOf(config), config.updates.apiUrl, githubToken(config.updates));
    const std::string product(config::productName(target));
    auto fetched = client.latest(config.updates.channel, product, "");
    if (!fetched) {
        return std::unexpected(fetched.error().message);
    }
    const auto& release = fetched->release;
    if (!release.version) {
        return std::unexpected("the latest release has no readable version");
    }
    const std::string platform =
        config.updates.assetPlatform.empty() ? github::hostPlatform() : config.updates.assetPlatform;
    const auto* asset = github::selectAsset(release, product, platform, github::hostArch());
    if (!asset) {
        return std::unexpected("the release " + core::toString(*release.version) + " has no " + product +
                               " binary for " + platform + "-" + github::hostArch());
    }
    auto staged = install::downloadVerified(client, release, *asset, layout);
    if (!staged) {
        return std::unexpected(staged.error());
    }
    state.installed.reset();
    state.etag = fetched->etag;
    auto done = install::activate(layout, state, *staged, {"", core::toString(*release.version), release.publishedAt},
                                  config.updates.keepPrevious);
    if (done) {
        logInfo("Installed " + core::toString(*release.version) + ".");
    }
    return done;
}

} // namespace basalt::hoist::runtime
