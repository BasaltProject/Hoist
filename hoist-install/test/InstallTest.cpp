#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <basalt/hoist/install/Install.hpp>
#include <basalt/hoist/net/Hash.hpp>

#include <doctest/doctest.h>

#include <fstream>

using namespace basalt::hoist;
namespace fs = std::filesystem;

namespace {

class FakeHttp final : public net::HttpClient {
public:
    std::map<std::string, std::string> files;

    std::expected<net::HttpResponse, std::string> request(const net::HttpRequest& request,
                                                          const net::BodySink& sink) override {
        auto it = files.find(request.url);
        if (it == files.end()) {
            return net::HttpResponse{404, {}, {}};
        }
        sink(it->second);
        return net::HttpResponse{200, {}, {}};
    }
};

github::Release sampleRelease() {
    github::Release release;
    release.tag = "v26.3.4";
    release.assets = {{"BasaltServer-26.3.4-Linux-x64", "https://x/bin", 0}, {"SHA256SUMS", "https://x/sums", 0}};
    return release;
}

} // namespace

TEST_CASE("an update is verified before it is kept, and a bad one is rolled back") {
    const fs::path dir = fs::temp_directory_path() / "hoist-install-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    install::Layout layout(dir);
    FakeHttp http;
    github::GitHubClient client(http, "x/y", "https://api.invalid", "");
    auto release = sampleRelease();

    http.files["https://x/bin"] = "new binary";
    http.files["https://x/sums"] = std::string(64, '0') + "  BasaltServer-26.3.4-Linux-x64\n";
    CHECK_FALSE(install::downloadVerified(client, release, release.assets[0], layout).has_value());
    CHECK_FALSE(fs::exists(layout.staging() / "BasaltServer-26.3.4-Linux-x64"));

    http.files["https://x/sums"] = net::sha256Hex("new binary") + "  BasaltServer-26.3.4-Linux-x64\n";
    auto staged = install::downloadVerified(client, release, release.assets[0], layout);
    REQUIRE(staged.has_value());

    install::State state;
    state.installed = install::Record{"BasaltServer-26.3.3-Linux-x64", "26.3.3", 0};
    std::ofstream(dir / "BasaltServer-26.3.3-Linux-x64") << "old binary";
    REQUIRE(install::activate(layout, state, *staged, {"", "26.3.4", 5}, 2).has_value());
    CHECK(state.installed->version == "26.3.4");
    CHECK(fs::exists(layout.resolve(*state.installed)));

    auto back = install::rollback(layout, state);
    REQUIRE(back.has_value());
    CHECK(back->version == "26.3.3");
    CHECK(state.rejected->version == "26.3.4");
    CHECK_FALSE(fs::exists(layout.binaries() / "BasaltServer-26.3.4-Linux-x64"));

    auto reloaded = install::loadState(layout.stateFile());
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->installed->version == "26.3.3");
    fs::remove_all(dir);
}
