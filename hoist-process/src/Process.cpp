#include <basalt/hoist/process/Process.hpp>

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace basalt::hoist::process {

namespace {

void passLines(Process& child, std::string& pending, const LineFilter& filter) {
    for (auto end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
        std::string line = pending.substr(0, end);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!filter || !filter(line)) {
            child.writeLine(line);
        }
        pending.erase(0, end + 1);
    }
}

} // namespace

#ifndef _WIN32
namespace {

class PosixProcess final : public Process {
public:
    PosixProcess(pid_t pid, int input) : pid_(pid), input_(input) {}

    ~PosixProcess() override {
        if (input_ >= 0) {
            ::close(input_);
        }
    }

    long id() const override { return pid_; }

    std::optional<ExitStatus> poll() override {
        if (finished_) {
            return status_;
        }
        int raw = 0;
        const pid_t result = ::waitpid(pid_, &raw, WNOHANG);
        if (result == 0) {
            return std::nullopt;
        }
        finished_ = true;
        if (result < 0) {
            status_ = ExitStatus{-1, false, 0};
        } else if (WIFSIGNALED(raw)) {
            status_ = ExitStatus{128 + WTERMSIG(raw), true, WTERMSIG(raw)};
        } else {
            status_ = ExitStatus{WEXITSTATUS(raw), false, 0};
        }
        return status_;
    }

    bool writeLine(std::string_view line) override {
        if (input_ < 0 || finished_) {
            return false;
        }
        std::string text(line);
        text += '\n';
        std::string_view rest = text;
        while (!rest.empty()) {
            const ssize_t written = ::write(input_, rest.data(), rest.size());
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            rest.remove_prefix(static_cast<std::size_t>(written));
        }
        return true;
    }

    void terminate() override {
        if (!finished_) {
            ::kill(pid_, SIGTERM);
        }
    }

    void kill() override {
        if (!finished_) {
            ::kill(pid_, SIGKILL);
        }
    }

    int input() const { return input_; }

private:
    pid_t pid_;
    int input_;
    bool finished_ = false;
    ExitStatus status_;
};

} // namespace

std::expected<std::unique_ptr<Process>, std::string> spawn(const LaunchSpec& spec) {
    int pipes[2];
    if (::pipe(pipes) != 0) {
        return std::unexpected(std::string("cannot create a pipe: ") + std::strerror(errno));
    }
    int errors[2];
    if (::pipe(errors) != 0) {
        ::close(pipes[0]);
        ::close(pipes[1]);
        return std::unexpected(std::string("cannot create a pipe: ") + std::strerror(errno));
    }
    ::fcntl(errors[1], F_SETFD, FD_CLOEXEC);
    ::fcntl(pipes[1], F_SETFD, FD_CLOEXEC);

    std::vector<std::string> storage{spec.command};
    storage.insert(storage.end(), spec.args.begin(), spec.args.end());
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& item : storage) {
        argv.push_back(item.data());
    }
    argv.push_back(nullptr);
    const std::string directory = spec.workingDirectory.string();

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipes[0]);
        ::close(pipes[1]);
        ::close(errors[0]);
        ::close(errors[1]);
        return std::unexpected(std::string("cannot fork: ") + std::strerror(errno));
    }
    if (pid == 0) {
        ::setpgid(0, 0);
        ::dup2(pipes[0], STDIN_FILENO);
        ::close(pipes[0]);
        if (!directory.empty() && ::chdir(directory.c_str()) != 0) {
            const int failure = errno;
            [[maybe_unused]] auto written = ::write(errors[1], &failure, sizeof failure);
            _exit(127);
        }
        ::execvp(argv[0], argv.data());
        const int failure = errno;
        [[maybe_unused]] auto written = ::write(errors[1], &failure, sizeof failure);
        _exit(127);
    }
    ::close(pipes[0]);
    ::close(errors[1]);
    int failure = 0;
    ssize_t got = 0;
    do {
        got = ::read(errors[0], &failure, sizeof failure);
    } while (got < 0 && errno == EINTR);
    ::close(errors[0]);
    if (got == static_cast<ssize_t>(sizeof failure)) {
        ::close(pipes[1]);
        ::waitpid(pid, nullptr, 0);
        return std::unexpected("cannot start " + spec.command + ": " + std::strerror(failure));
    }
    return std::make_unique<PosixProcess>(pid, pipes[1]);
}

void forwardInput(Process& child, const std::atomic<bool>& stop, const LineFilter& filter) {
    std::string pending;
    while (!stop && !child.poll()) {
        pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 200);
        if (ready <= 0) {
            continue;
        }
        if (descriptor.revents & (POLLERR | POLLNVAL)) {
            return;
        }
        char buffer[1024];
        const ssize_t got = ::read(STDIN_FILENO, buffer, sizeof buffer);
        if (got <= 0) {
            return;
        }
        pending.append(buffer, static_cast<std::size_t>(got));
        passLines(child, pending, filter);
    }
}

void stopInput(std::jthread& thread, std::atomic<bool>& stop) {
    stop = true;
    thread = {};
}

#else

namespace {

class WindowsProcess final : public Process {
public:
    WindowsProcess(HANDLE process, HANDLE input, DWORD id) : process_(process), input_(input), id_(id) {}

    ~WindowsProcess() override {
        CloseHandle(input_);
        CloseHandle(process_);
    }

    long id() const override { return static_cast<long>(id_); }

    std::optional<ExitStatus> poll() override {
        DWORD code = 0;
        if (GetExitCodeProcess(process_, &code) && code != STILL_ACTIVE) {
            return ExitStatus{static_cast<int>(code), false, 0};
        }
        return std::nullopt;
    }

    bool writeLine(std::string_view line) override {
        std::string text(line);
        text += "\r\n";
        DWORD written = 0;
        return WriteFile(input_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0;
    }

    void terminate() override {}

    void kill() override { TerminateProcess(process_, 1); }

private:
    HANDLE process_;
    HANDLE input_;
    DWORD id_;
};

} // namespace

std::expected<std::unique_ptr<Process>, std::string> spawn(const LaunchSpec& spec) {
    SECURITY_ATTRIBUTES attributes{sizeof attributes, nullptr, TRUE};
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (!CreatePipe(&read, &write, &attributes, 0)) {
        return std::unexpected("cannot create a pipe");
    }
    SetHandleInformation(write, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = read;
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    std::string line = "\"" + spec.command + "\"";
    for (const auto& arg : spec.args) {
        line += " \"" + arg + "\"";
    }
    PROCESS_INFORMATION info{};
    const std::string directory = spec.workingDirectory.string();
    const BOOL created = CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NEW_PROCESS_GROUP, nullptr,
                                        directory.empty() ? nullptr : directory.c_str(), &startup, &info);
    CloseHandle(read);
    if (!created) {
        CloseHandle(write);
        return std::unexpected("cannot start " + spec.command);
    }
    CloseHandle(info.hThread);
    return std::make_unique<WindowsProcess>(info.hProcess, write, info.dwProcessId);
}

void forwardInput(Process& child, const std::atomic<bool>& stop, const LineFilter& filter) {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE) {
        return;
    }
    std::string pending;
    while (!stop && !child.poll()) {
        char buffer[1024];
        DWORD got = 0;
        if (ReadFile(input, buffer, sizeof buffer, &got, nullptr) == 0 || got == 0) {
            return;
        }
        pending.append(buffer, got);
        passLines(child, pending, filter);
    }
}

// A read on the console cannot be polled, so the blocked read is cancelled until the thread is gone.
void stopInput(std::jthread& thread, std::atomic<bool>& stop) {
    stop = true;
    if (thread.joinable()) {
        const HANDLE handle = thread.native_handle();
        while (WaitForSingleObject(handle, 50) == WAIT_TIMEOUT) {
            CancelSynchronousIo(handle);
        }
    }
    thread = {};
}

#endif

} // namespace basalt::hoist::process
