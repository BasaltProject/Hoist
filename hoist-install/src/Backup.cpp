#include <basalt/hoist/install/Install.hpp>

#include <algorithm>
#include <ctime>
#include <vector>

namespace basalt::hoist::install {
namespace fs = std::filesystem;

namespace {

std::uintmax_t sizeOf(const fs::path& path) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        return fs::file_size(path, ec);
    }
    std::uintmax_t total = 0;
    for (const auto& entry :
         fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

std::string stamp(std::int64_t now) {
    const auto raw = static_cast<std::time_t>(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &raw);
#else
    gmtime_r(&raw, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y%m%d-%H%M%S", &tm);
    return buffer;
}

} // namespace

std::expected<fs::path, std::string> backupData(const Layout& layout, const config::BackupConfig& settings,
                                                std::string_view from, std::string_view to, std::int64_t now) {
    const fs::path root = fs::path(settings.destination).is_absolute()
                              ? fs::path(settings.destination)
                              : layout.workingDirectory() / settings.destination;
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        return std::unexpected("cannot create " + root.string() + ": " + ec.message());
    }
    std::uintmax_t needed = 0;
    std::vector<fs::path> sources;
    for (const auto& item : settings.paths) {
        const fs::path source = layout.workingDirectory() / item;
        if (fs::exists(source, ec)) {
            needed += sizeOf(source);
            sources.push_back(source);
        }
    }
    if (const auto space = fs::space(root, ec); !ec && space.available < needed + (needed / 10)) {
        return std::unexpected("not enough free space for the backup (" + std::to_string(needed) + " bytes needed)");
    }
    const fs::path folder = root / (stamp(now) + "-" + std::string(from) + "-to-" + std::string(to));
    fs::create_directories(folder, ec);
    for (const auto& source : sources) {
        const fs::path target = folder / fs::relative(source, layout.workingDirectory());
        fs::create_directories(target.parent_path(), ec);
        fs::copy(source, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fs::remove_all(folder);
            return std::unexpected("cannot copy " + source.string() + ": " + ec.message());
        }
    }
    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.is_directory(ec) && entry.path().filename().string().find("-to-") != std::string::npos) {
            folders.push_back(entry.path());
        }
    }
    std::ranges::sort(folders);
    while (folders.size() > static_cast<std::size_t>(settings.keep)) {
        fs::remove_all(folders.front(), ec);
        folders.erase(folders.begin());
    }
    return folder;
}

} // namespace basalt::hoist::install
