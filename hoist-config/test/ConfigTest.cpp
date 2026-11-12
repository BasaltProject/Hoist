#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <basalt/hoist/config/Config.hpp>

#include <doctest/doctest.h>

#include <filesystem>

using namespace basalt::hoist::config;

TEST_CASE("the generated defaults load and are safe: nothing is applied without a mode") {
    const auto dir = std::filesystem::temp_directory_path() / "hoist-config-test-defaults";
    std::filesystem::remove_all(dir);
    auto config = loadConfigDirectory(dir, Target::Proxy);
    REQUIRE(config.has_value());
    CHECK(config->launcher.target == Target::Proxy);
    CHECK_FALSE(config->schedule.dailyRestart.enabled);
    CHECK_FALSE(config->schedule.immediate.enabled);
    CHECK(config->schedule.conditions.empty());
    CHECK(config->warnings.empty());
    std::filesystem::remove_all(dir);
}
