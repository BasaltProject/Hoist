#include <basalt/hoist/runtime/Lock.hpp>

#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace basalt::hoist::runtime {
namespace {

#ifndef _WIN32
class PosixLock final : public FolderLock {
public:
    explicit PosixLock(int descriptor) : descriptor_(descriptor) {}

    ~PosixLock() override { ::close(descriptor_); }

private:
    int descriptor_;
};
#endif

} // namespace

std::unique_ptr<FolderLock> acquireFolderLock(const std::filesystem::path& file) {
#ifndef _WIN32
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const int descriptor = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (descriptor < 0) {
        return nullptr;
    }
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        ::close(descriptor);
        return nullptr;
    }
    const std::string pid = std::to_string(::getpid()) + "\n";
    if (::ftruncate(descriptor, 0) == 0) {
        [[maybe_unused]] auto written = ::write(descriptor, pid.data(), pid.size());
    }
    return std::make_unique<PosixLock>(descriptor);
#else
    (void)file;
    return std::unique_ptr<FolderLock>(new FolderLock{});
#endif
}

bool folderLockHeld(const std::filesystem::path& file) {
#ifndef _WIN32
    if (!std::filesystem::exists(file)) {
        return false;
    }
    return acquireFolderLock(file) == nullptr;
#else
    (void)file;
    return false;
#endif
}

} // namespace basalt::hoist::runtime
