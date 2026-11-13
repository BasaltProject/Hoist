#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <vector>

namespace basalt::hoist::install::zip {

struct Skipped {
    static constexpr const char* kMessage = "unreadable";
};

class Writer {
public:
    static std::expected<Writer, std::string> create(const std::filesystem::path& file, std::uint16_t dosTime,
                                                     std::uint16_t dosDate);

    // Returns Skipped when the file cannot be opened, so a file that vanished or is locked does not sink a backup.
    std::expected<void, std::string> addFile(const std::string& name, const std::filesystem::path& source,
                                             std::uint32_t mode, std::stop_token stop);
    std::expected<void, std::string> addDirectory(const std::string& name, std::uint32_t mode);
    std::expected<void, std::string> finish();

private:
    struct Entry {
        std::string name;
        std::uint32_t crc = 0;
        std::uint64_t compressed = 0;
        std::uint64_t size = 0;
        std::uint64_t offset = 0;
        std::uint32_t mode = 0;
    };

    std::expected<void, std::string> add(const std::string& name, const std::filesystem::path* source,
                                         std::uint32_t mode, std::stop_token stop);

    Writer() = default;

    void put(const std::string& bytes);

    std::ofstream out_;
    std::uint64_t written_ = 0;
    std::uint16_t time_ = 0;
    std::uint16_t date_ = 0;
    std::vector<Entry> entries_;
};

std::expected<void, std::string> extract(const std::filesystem::path& file, const std::filesystem::path& destination);

std::pair<std::uint16_t, std::uint16_t> dosStamp(std::int64_t epoch);

} // namespace basalt::hoist::install::zip
