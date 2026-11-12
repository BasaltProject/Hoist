#pragma once

#include <basalt/hoist/runtime/Supervisor.hpp>

namespace basalt::hoist::runtime {

int commandInit(const RunOptions& options);
int commandCheck(const RunOptions& options);
int commandStatus(const RunOptions& options);
int commandUpdate(const RunOptions& options);
int commandRollback(const RunOptions& options);

} // namespace basalt::hoist::runtime
