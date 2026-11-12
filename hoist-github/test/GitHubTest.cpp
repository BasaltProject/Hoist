#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <basalt/hoist/github/GitHub.hpp>

#include <doctest/doctest.h>

using namespace basalt::hoist;

namespace {

// The shape of the real nightly release, trimmed.
constexpr std::string_view kNightly = R"({
  "tag_name": "nightly", "name": "Nightly 26.3.3-n3", "prerelease": true, "published_at": "2026-10-31T17:45:36Z",
  "assets": [
    {"name": "BasaltServer-26.3.3-n3-Linux-musl-x64", "browser_download_url": "https://x/musl", "size": 10},
    {"name": "BasaltServer-26.3.3-n3-Linux-x64", "browser_download_url": "https://x/linux", "size": 11},
    {"name": "BasaltServer-26.3.3-n3-Windows-x64.exe", "browser_download_url": "https://x/win", "size": 12},
    {"name": "SHA256SUMS", "browser_download_url": "https://x/sums", "size": 1}
  ]})";

} // namespace

TEST_CASE("a release names its version and the right binary for each platform") {
    auto json = net::parseJson(kNightly);
    REQUIRE(json.has_value());
    auto release = github::parseRelease(*json, "BasaltServer");
    REQUIRE(release.has_value());
    CHECK(core::toString(*release->version) == "26.3.3-n3");
    CHECK(release->prerelease);
    CHECK(github::selectAsset(*release, "BasaltServer", "Linux", "x64")->url == "https://x/linux");
    CHECK(github::selectAsset(*release, "BasaltServer", "Linux-musl", "x64")->url == "https://x/musl");
    CHECK(github::selectAsset(*release, "BasaltServer", "Windows", "x64")->url == "https://x/win");
    CHECK(github::selectAsset(*release, "BasaltServer", "macOS", "arm64") == nullptr);
    CHECK(github::selectAsset(*release, "BasaltProxy", "Linux", "x64") == nullptr);
}

TEST_CASE("SHA256SUMS is read in the sha256sum format") {
    const std::string sums = std::string(64, 'a') + "  BasaltServer-26.3.3-Linux-x64\n" + std::string(64, 'B') +
                             " *BasaltServer-26.3.3-Windows-x64.exe\n";
    CHECK(github::checksumFor(sums, "BasaltServer-26.3.3-Linux-x64") == std::string(64, 'a'));
    CHECK(github::checksumFor(sums, "BasaltServer-26.3.3-Windows-x64.exe") == std::string(64, 'b'));
    CHECK_FALSE(github::checksumFor(sums, "missing"));
}
