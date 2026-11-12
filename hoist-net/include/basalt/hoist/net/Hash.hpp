#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace basalt::hoist::net {

std::string sha256Hex(std::string_view data);
std::expected<std::string, std::string> sha256File(const std::filesystem::path& file);

} // namespace basalt::hoist::net
