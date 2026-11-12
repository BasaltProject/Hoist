#pragma once

#include <filesystem>
#include <memory>

namespace basalt::hoist::runtime {

class FolderLock {
public:
    virtual ~FolderLock() = default;
};

std::unique_ptr<FolderLock> acquireFolderLock(const std::filesystem::path& file);
bool folderLockHeld(const std::filesystem::path& file);

} // namespace basalt::hoist::runtime
