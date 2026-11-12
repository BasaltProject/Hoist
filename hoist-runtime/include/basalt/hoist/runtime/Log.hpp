#pragma once

#include <string_view>

namespace basalt::hoist::runtime {

void logInfo(std::string_view message);
void logWarn(std::string_view message);
void logError(std::string_view message);

} // namespace basalt::hoist::runtime
