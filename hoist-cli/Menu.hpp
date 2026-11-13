#pragma once

#include <basalt/hoist/runtime/Supervisor.hpp>

namespace basalt::hoist::cli {

int runMenu(runtime::RunOptions options, const char* version, const char* help);

} // namespace basalt::hoist::cli
