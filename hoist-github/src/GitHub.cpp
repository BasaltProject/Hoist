#include <basalt/hoist/core/Time.hpp>
#include <basalt/hoist/github/GitHub.hpp>

#include <algorithm>
#include <charconv>

namespace basalt::hoist::github {
namespace {

constexpr std::string_view kPlatforms[] = {"Linux-musl", "Linux", "macOS", "Windows", "FreeBSD", "Android"};

std::string lowerCase(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::optional<std::string> versionOfAsset(std::string_view name, std::string_view product) {
    if (!name.starts_with(product) || name.size() <= product.size() + 1 || name[product.size()] != '-') {
        return std::nullopt;
    }
    name.remove_prefix(product.size() + 1);
    for (auto platform : kPlatforms) {
        if (const auto at = name.find("-" + std::string(platform)); at != std::string_view::npos && at > 0) {
            return std::string(name.substr(0, at));
        }
    }
    return std::nullopt;
}

} // namespace

std::string hostPlatform() {
#if defined(__ANDROID__)
    return "Android";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__FreeBSD__)
    return "FreeBSD";
#elif defined(__linux__) && !defined(__GLIBC__)
    return "Linux-musl";
#else
    return "Linux";
#endif
}

std::string hostArch() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#else
    return "x64";
#endif
}

std::expected<Release, std::string> parseRelease(const net::Json& json, std::string_view product) {
    if (json.kind != net::Json::Kind::Object) {
        return std::unexpected("the release answer is not an object");
    }
    Release release;
    release.tag = json.stringOr("tag_name");
    release.title = json.stringOr("name");
    release.prerelease = json.boolOr("prerelease");
    if (auto published = core::parseIso8601(json.stringOr("published_at"))) {
        release.publishedAt = *published;
    }
    if (const auto* assets = json.find("assets"); assets && assets->kind == net::Json::Kind::Array) {
        for (const auto& item : assets->values) {
            Asset asset{item.stringOr("name"), item.stringOr("browser_download_url"),
                        static_cast<std::int64_t>(item.numberOr("size"))};
            if (!asset.name.empty() && !asset.url.empty()) {
                release.assets.push_back(std::move(asset));
            }
        }
    }
    if (release.tag.empty()) {
        return std::unexpected("the release has no tag");
    }
    for (const auto& asset : release.assets) {
        if (auto text = versionOfAsset(asset.name, product)) {
            release.version = core::parseVersion(*text);
            break;
        }
    }
    if (!release.version) {
        std::string_view title = release.title;
        if (title.starts_with("Nightly ")) {
            title.remove_prefix(8);
        }
        release.version = core::parseVersion(title);
    }
    if (!release.version) {
        release.version = core::parseVersion(release.tag);
    }
    return release;
}

const Asset* findAsset(const Release& release, std::string_view name) {
    for (const auto& asset : release.assets) {
        if (asset.name == name) {
            return &asset;
        }
    }
    return nullptr;
}

const Asset* selectAsset(const Release& release, std::string_view product, std::string_view platform,
                         std::string_view arch) {
    const std::string suffix = "-" + std::string(platform) + "-" + std::string(arch);
    for (const auto& asset : release.assets) {
        std::string_view name = asset.name;
        if (name.ends_with(".exe")) {
            name.remove_suffix(4);
        }
        if (name.starts_with(product) && name.ends_with(suffix) && versionOfAsset(asset.name, product)) {
            return &asset;
        }
    }
    return nullptr;
}

std::optional<std::string> checksumFor(std::string_view sums, std::string_view assetName) {
    while (!sums.empty()) {
        const auto end = sums.find('\n');
        std::string_view line = sums.substr(0, end);
        sums = end == std::string_view::npos ? std::string_view{} : sums.substr(end + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.remove_suffix(1);
        }
        const auto space = line.find(' ');
        if (space != 64 || line.size() < 66) {
            continue;
        }
        std::string_view name = line.substr(65);
        if (name.starts_with('*') || name.starts_with(' ')) {
            name.remove_prefix(1);
        }
        if (name == assetName) {
            return lowerCase(std::string(line.substr(0, 64)));
        }
    }
    return std::nullopt;
}

std::expected<Fetched, FetchError> GitHubClient::latest(core::Channel channel, std::string_view product,
                                                        std::string_view etag) const {
    net::HttpRequest request;
    request.url = apiUrl_ + "/repos/" + repository_ +
                  (channel == core::Channel::Release ? "/releases/latest" : "/releases/tags/nightly");
    request.headers = {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"}};
    if (!token_.empty()) {
        request.headers.emplace_back("Authorization", "Bearer " + token_);
    }
    if (!etag.empty()) {
        request.headers.emplace_back("If-None-Match", std::string(etag));
    }
    request.totalTimeoutSeconds = 60;
    auto response = net::httpGet(*http_, request);
    if (!response) {
        return std::unexpected(FetchError{response.error(), 0});
    }
    if (response->status == 304) {
        return Fetched{false, {}, std::string(etag)};
    }
    if (response->status == 403 || response->status == 429) {
        FetchError error{"GitHub refused the request (rate limit or access): " + std::to_string(response->status), 0};
        if (auto retry = response->headers.find("retry-after"); retry != response->headers.end()) {
            std::from_chars(retry->second.data(), retry->second.data() + retry->second.size(), error.retryAfterSeconds);
        } else if (auto reset = response->headers.find("x-ratelimit-reset"); reset != response->headers.end()) {
            std::int64_t at = 0;
            std::from_chars(reset->second.data(), reset->second.data() + reset->second.size(), at);
            error.retryAfterSeconds = static_cast<int>(std::max<std::int64_t>(60, at - std::time(nullptr)));
        }
        return std::unexpected(error);
    }
    if (response->status == 404) {
        return std::unexpected(FetchError{"GitHub has no such release in " + repository_ + " (404)", 0});
    }
    if (response->status < 200 || response->status >= 300) {
        return std::unexpected(FetchError{"GitHub answered " + std::to_string(response->status), 0});
    }
    auto json = net::parseJson(response->body);
    if (!json) {
        return std::unexpected(FetchError{"GitHub sent unreadable JSON: " + json.error(), 0});
    }
    auto release = parseRelease(*json, product);
    if (!release) {
        return std::unexpected(FetchError{release.error(), 0});
    }
    Fetched fetched{true, std::move(*release), {}};
    if (auto tag = response->headers.find("etag"); tag != response->headers.end()) {
        fetched.etag = tag->second;
    }
    return fetched;
}

net::HttpRequest GitHubClient::downloadRequest(const Asset& asset) const {
    net::HttpRequest request;
    request.url = asset.url;
    request.maxBodyBytes = std::size_t{2} << 30;
    request.idleTimeoutSeconds = 30;
    request.bytesPerSecond = downloadBytesPerSecond_;
    return request;
}

std::expected<std::string, std::string> GitHubClient::fetchText(const Asset& asset) const {
    auto request = downloadRequest(asset);
    request.maxBodyBytes = 1u << 20;
    request.totalTimeoutSeconds = 60;
    auto response = net::httpGet(*http_, request);
    if (!response) {
        return std::unexpected(response.error());
    }
    if (response->status != 200) {
        return std::unexpected("downloading " + asset.name + " answered " + std::to_string(response->status));
    }
    return response->body;
}

} // namespace basalt::hoist::github
