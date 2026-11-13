#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <basalt/hoist/install/Backup.hpp>
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

TEST_CASE("a backup keeps what is configured, leaves out what is excluded and restores it") {
    const fs::path dir = fs::temp_directory_path() / "hoist-backup-test";
    fs::remove_all(dir);
    fs::create_directories(dir / "worlds" / "w");
    std::ofstream(dir / "worlds" / "w" / "level.dat") << std::string(200000, 'a');
    std::ofstream(dir / "worlds" / "w" / "debug.log") << "noise";
    std::ofstream(dir / "players.db") << "players";
    fs::create_directories(dir / "worlds" / "empty");
    std::ofstream(dir / "worlds" / "run.sh") << "#!/bin/sh";
    fs::permissions(dir / "worlds" / "run.sh", fs::perms::owner_all, fs::perm_options::replace);
    for (const bool compress : {false, true}) {
        config::BackupConfig settings;
        settings.paths = {"worlds", "players.db"};
        settings.exclude = {"*.log"};
        settings.compress = compress;
        const install::Layout layout(dir);
        auto made = install::createBackup(layout, settings, "test", 1800000000);
        REQUIRE(made);
        CHECK(made->files == 3);
        CHECK(made->zipped == compress);
        fs::remove_all(dir / "worlds");
        std::ofstream(dir / "players.db", std::ios::trunc) << "changed";
        auto restored = install::restoreBackup(layout, settings, "latest", 1800000100);
        REQUIRE(restored);
        CHECK(fs::file_size(dir / "worlds" / "w" / "level.dat") == 200000);
        CHECK_FALSE(fs::exists(dir / "worlds" / "w" / "debug.log"));
        CHECK(fs::is_directory(dir / "worlds" / "empty"));
        CHECK((fs::status(dir / "worlds" / "run.sh").permissions() & fs::perms::owner_exec) != fs::perms::none);
        std::string players;
        std::ifstream(dir / "players.db") >> players;
        CHECK(players == "players");
        fs::remove_all(install::backupRoot(layout, settings));
        fs::create_directories(dir / "worlds" / "w");
        fs::create_directories(dir / "worlds" / "empty");
    }
    fs::remove_all(dir);
}

TEST_CASE("retention keeps the newest, one per day and one per week, and never the oldest alone") {
    std::vector<install::BackupInfo> all;
    const std::int64_t base = 1800000000;
    for (int hours = 0; hours < 24 * 20; hours += 6) {
        install::BackupInfo info;
        info.created = base - std::int64_t{hours} * 3600;
        info.name = std::to_string(info.created);
        info.size = 1024 * 1024;
        all.push_back(info);
    }
    config::BackupRetention retention;
    retention.keepLast = 2;
    CHECK(install::backupsToPrune(all, retention, base).size() == all.size() - 2);
    retention.keepDaily = 3;
    CHECK(install::backupsToPrune(all, retention, base).size() == all.size() - 4);
    retention.maxTotalMib = 1;
    CHECK(install::backupsToPrune(all, retention, base).size() == all.size() - 1);
    CHECK(install::matchesExclude("worlds/w/debug.log", {"*.log"}));
    CHECK(install::matchesExclude("cache/a/b", {"cache"}));
    CHECK_FALSE(install::matchesExclude("worlds/w/level.dat", {"*.log", "cache"}));
}
