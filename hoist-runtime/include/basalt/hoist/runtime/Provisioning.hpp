#pragma once

#include <basalt/hoist/config/Config.hpp>
#include <basalt/hoist/install/Install.hpp>

#include <expected>
#include <string>

namespace basalt::hoist::runtime {

std::string githubToken(const config::UpdatesConfig& updates);
std::string repositoryOf(const config::Config& config);

std::expected<void, std::string> ensureInstalled(const config::Config& config, const install::Layout& layout,
                                                 install::State& state);

} // namespace basalt::hoist::runtime
