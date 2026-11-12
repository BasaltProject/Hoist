#pragma once

#include <basalt/hoist/core/Version.hpp>
#include <basalt/hoist/net/Http.hpp>
#include <basalt/hoist/net/Json.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace basalt::hoist::github {

struct Asset {
    std::string name;
    std::string url;
    std::int64_t size = 0;
};

struct Release {
    std::string tag;
    std::string title;
    bool prerelease = false;
    std::int64_t publishedAt = 0;
    std::optional<core::Version> version;
    std::vector<Asset> assets;
};

struct FetchError {
    std::string message;
    int retryAfterSeconds = 0; // above zero when GitHub asked to wait
};

struct Fetched {
    bool modified = true; // false when the ETag we sent is still current
    Release release;
    std::string etag;
};

// The names the publishing scripts give the binaries: BasaltServer-26.3.3-Linux-x64, ...-Windows-x64.exe
std::string hostPlatform();
std::string hostArch();

std::expected<Release, std::string> parseRelease(const net::Json& json, std::string_view product);
const Asset* selectAsset(const Release& release, std::string_view product, std::string_view platform,
                         std::string_view arch);
const Asset* findAsset(const Release& release, std::string_view name);
std::optional<std::string> checksumFor(std::string_view sums, std::string_view assetName);

class GitHubClient {
public:
    GitHubClient(net::HttpClient& http, std::string repository, std::string apiUrl, std::string token)
        : http_(&http), repository_(std::move(repository)), apiUrl_(std::move(apiUrl)), token_(std::move(token)) {}

    std::expected<Fetched, FetchError> latest(core::Channel channel, std::string_view product,
                                              std::string_view etag) const;
    std::expected<std::string, std::string> fetchText(const Asset& asset) const;
    net::HttpRequest downloadRequest(const Asset& asset) const;

    net::HttpClient* http() const { return http_; }

private:
    net::HttpClient* http_;
    std::string repository_;
    std::string apiUrl_;
    std::string token_;
};

} // namespace basalt::hoist::github
