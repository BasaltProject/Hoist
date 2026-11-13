#include "Zip.hpp"

#include <basalt/hoist/core/Time.hpp>
#include <basalt/hoist/install/Backup.hpp>

#include <algorithm>
#include <ctime>
#include <map>
#include <set>

namespace basalt::hoist::install {
namespace fs = std::filesystem;

namespace {

constexpr std::string_view kPartial = ".partial";
constexpr std::int64_t kDay = 86400;

struct Item {
    fs::path source;
    std::string relative;
    std::uintmax_t size = 0;
    bool directory = false;
};

constexpr std::string_view kSafetyKind = "pre-restore";

std::uint32_t modeOf(const fs::path& path) {
    std::error_code ec;
    return static_cast<std::uint32_t>(fs::status(path, ec).permissions() & fs::perms::mask) & 0x1FFu;
}

std::expected<void, std::string> copyChunked(const fs::path& from, const fs::path& to, const std::stop_token& stop) {
    std::ifstream in(from, std::ios::binary);
    if (!in) {
        return std::unexpected(std::string(zip::Skipped::kMessage));
    }
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected("cannot write " + to.string());
    }
    std::vector<char> buffer(std::size_t{256} * 1024);
    while (in) {
        if (stop.stop_requested()) {
            return std::unexpected("the backup was cancelled");
        }
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        out.write(buffer.data(), in.gcount());
    }
    out.close();
    if (!out) {
        return std::unexpected("cannot write " + to.string());
    }
    std::error_code ec;
    fs::permissions(to, fs::status(from, ec).permissions() & fs::perms::mask, ec);
    return {};
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

std::optional<std::int64_t> parseStamp(std::string_view name) {
    if (name.size() < 15 || name[8] != '-') {
        return std::nullopt;
    }
    const auto number = [&](std::size_t at, std::size_t length) -> int {
        int value = 0;
        for (std::size_t i = at; i < at + length; ++i) {
            if (name[i] < '0' || name[i] > '9') {
                return -1;
            }
            value = value * 10 + (name[i] - '0');
        }
        return value;
    };
    const int y = number(0, 4);
    const int mo = number(4, 2);
    const int d = number(6, 2);
    const int h = number(9, 2);
    const int mi = number(11, 2);
    const int s = number(13, 2);
    if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60) {
        return std::nullopt;
    }
    return core::epochFromUtc(y, mo, d, h, mi, s);
}

std::string cleanKind(std::string_view kind) {
    std::string out;
    for (const char c : kind) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        out.push_back(ok ? c : '-');
    }
    return out.empty() ? "manual" : out;
}

std::uintmax_t folderSize(const fs::path& path) {
    std::error_code ec;
    std::uintmax_t total = 0;
    for (const auto& entry :
         fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

bool globMatch(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) {
        return text.empty();
    }
    if (pattern.starts_with("**")) {
        std::string_view rest = pattern.substr(2);
        while (rest.starts_with('*')) {
            rest.remove_prefix(1);
        }
        for (std::size_t i = 0; i <= text.size(); ++i) {
            if (globMatch(rest, text.substr(i))) {
                return true;
            }
        }
        return false;
    }
    if (pattern.front() == '*') {
        for (std::size_t i = 0; i <= text.size(); ++i) {
            if (globMatch(pattern.substr(1), text.substr(i))) {
                return true;
            }
            if (i < text.size() && text[i] == '/') {
                break;
            }
        }
        return false;
    }
    if (text.empty()) {
        return false;
    }
    if ((pattern.front() == '?' && text.front() != '/') || pattern.front() == text.front()) {
        return globMatch(pattern.substr(1), text.substr(1));
    }
    return false;
}

std::vector<std::string_view> components(std::string_view path) {
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        const auto slash = path.find('/');
        parts.push_back(path.substr(0, slash));
        path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
    }
    return parts;
}

void collect(const fs::path& root, const fs::path& source, const std::vector<std::string>& exclude,
             const fs::path& skipRoot, std::vector<Item>& items) {
    std::error_code ec;
    const std::string relative = fs::relative(source, root, ec).generic_string();
    if (relative.empty() || relative == ".hoist" || relative.starts_with(".hoist/") ||
        matchesExclude(relative, exclude)) {
        return;
    }
    if (std::error_code same; !skipRoot.empty() && fs::equivalent(source, skipRoot, same)) {
        return;
    }
    if (fs::is_symlink(source, ec)) {
        return;
    }
    if (fs::is_regular_file(source, ec)) {
        items.push_back({source, relative, fs::file_size(source, ec)});
        return;
    }
    std::vector<fs::path> children;
    for (const auto& entry : fs::directory_iterator(source, fs::directory_options::skip_permission_denied, ec)) {
        children.push_back(entry.path());
    }
    if (children.empty()) {
        items.push_back({source, relative, 0, true});
    }
    std::ranges::sort(children);
    for (const auto& child : children) {
        collect(root, child, exclude, skipRoot, items);
    }
}

BackupInfo describe(const fs::path& path) {
    BackupInfo info;
    std::string name = path.filename().string();
    info.zipped = name.ends_with(".zip");
    if (info.zipped) {
        name.resize(name.size() - 4);
    }
    info.name = name;
    info.kind = name.size() > 16 ? name.substr(16) : "";
    info.created = parseStamp(name).value_or(0);
    info.path = path;
    std::error_code ec;
    info.size = info.zipped ? fs::file_size(path, ec) : folderSize(path);
    return info;
}

} // namespace

bool matchesExclude(std::string_view relative, const std::vector<std::string>& patterns) {
    const auto parts = components(relative);
    for (const auto& raw : patterns) {
        std::string_view pattern = raw;
        while (pattern.starts_with('/')) {
            pattern.remove_prefix(1);
        }
        while (pattern.ends_with('/')) {
            pattern.remove_suffix(1);
        }
        if (pattern.empty()) {
            continue;
        }
        if (pattern.find('/') != std::string_view::npos) {
            std::string_view prefix = relative;
            while (true) {
                if (globMatch(pattern, prefix)) {
                    return true;
                }
                const auto slash = prefix.rfind('/');
                if (slash == std::string_view::npos) {
                    break;
                }
                prefix = prefix.substr(0, slash);
            }
            continue;
        }
        for (const auto part : parts) {
            if (globMatch(pattern, part)) {
                return true;
            }
        }
    }
    return false;
}

fs::path backupRoot(const Layout& layout, const config::BackupConfig& settings) {
    const fs::path destination(settings.destination);
    return destination.is_absolute() ? destination : layout.workingDirectory() / destination;
}

std::vector<BackupInfo> listBackups(const fs::path& root) {
    std::vector<BackupInfo> found;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.ends_with(kPartial) || !parseStamp(name)) {
            continue;
        }
        const bool zipped = name.ends_with(".zip") && entry.is_regular_file(ec);
        if (zipped || entry.is_directory(ec)) {
            found.push_back(describe(entry.path()));
        }
    }
    std::ranges::sort(found, [](const BackupInfo& a, const BackupInfo& b) { return a.name > b.name; });
    return found;
}

std::vector<std::string> backupsToPrune(const std::vector<BackupInfo>& newestFirst,
                                        const config::BackupRetention& retention, std::int64_t now) {
    std::set<std::size_t> keep;
    std::set<std::int64_t> days;
    std::set<std::int64_t> weeks;
    std::size_t counted = 0;
    std::size_t safety = 0;
    for (std::size_t i = 0; i < newestFirst.size(); ++i) {
        if (newestFirst[i].kind == kSafetyKind) {
            if (++safety <= 2) {
                keep.insert(i);
            }
            continue;
        }
        const std::int64_t day = newestFirst[i].created / kDay;
        const std::int64_t week = (day + 3) / 7;
        const std::int64_t today = now / kDay;
        if (static_cast<int>(counted++) < retention.keepLast) {
            keep.insert(i);
        }
        if (retention.keepDaily > 0 && today - day < retention.keepDaily && days.insert(day).second) {
            keep.insert(i);
        }
        if (retention.keepWeekly > 0 && (today + 3) / 7 - week < retention.keepWeekly && weeks.insert(week).second) {
            keep.insert(i);
        }
    }
    std::size_t newest = 0;
    while (newest < newestFirst.size() && newestFirst[newest].kind == kSafetyKind) {
        ++newest;
    }
    if (retention.maxAgeDays > 0) {
        for (auto it = keep.begin(); it != keep.end();) {
            const bool tooOld = now - newestFirst[*it].created > std::int64_t{retention.maxAgeDays} * kDay;
            it = (tooOld && *it != newest) ? keep.erase(it) : std::next(it);
        }
    }
    if (retention.maxTotalMib > 0) {
        const std::uintmax_t limit = std::uintmax_t{static_cast<unsigned>(retention.maxTotalMib)} * 1024 * 1024;
        std::uintmax_t total = 0;
        for (const auto index : keep) {
            total += newestFirst[index].size;
        }
        while (total > limit && keep.size() > 1 && *std::prev(keep.end()) != newest) {
            const auto oldest = std::prev(keep.end());
            total -= newestFirst[*oldest].size;
            keep.erase(oldest);
        }
    }
    std::vector<std::string> doomed;
    for (std::size_t i = 0; i < newestFirst.size(); ++i) {
        if (!keep.contains(i)) {
            doomed.push_back(newestFirst[i].name);
        }
    }
    return doomed;
}

std::expected<BackupInfo, std::string> findBackup(const fs::path& root, std::string_view name) {
    const auto all = listBackups(root);
    if (all.empty()) {
        return std::unexpected("there are no backups in " + root.string());
    }
    if (name == "latest") {
        for (const auto& info : all) {
            if (info.kind != kSafetyKind) {
                return info;
            }
        }
        return all.front();
    }
    std::string wanted(name);
    if (wanted.ends_with(".zip")) {
        wanted.resize(wanted.size() - 4);
    }
    for (const auto& info : all) {
        if (info.name == wanted) {
            return info;
        }
    }
    return std::unexpected("there is no backup named " + wanted + "; `hoist backup list` shows them");
}

std::expected<void, std::string> deleteBackup(const fs::path& root, std::string_view name) {
    auto found = findBackup(root, name);
    if (!found) {
        return std::unexpected(found.error());
    }
    std::error_code ec;
    fs::remove_all(found->path, ec);
    if (ec) {
        return std::unexpected("cannot delete " + found->path.string() + ": " + ec.message());
    }
    return {};
}

std::expected<BackupInfo, std::string> createBackup(const Layout& layout, const config::BackupConfig& settings,
                                                    std::string_view kind, std::int64_t now, std::stop_token stop) {
    const fs::path root = backupRoot(layout, settings);
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        return std::unexpected("cannot create " + root.string() + ": " + ec.message());
    }
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (entry.path().filename().string().ends_with(kPartial)) {
            fs::remove_all(entry.path(), ec);
        }
    }
    const fs::path skipRoot = fs::weakly_canonical(root, ec);
    std::vector<Item> items;
    std::uintmax_t needed = 0;
    for (const auto& path : settings.paths) {
        const fs::path source = layout.workingDirectory() / path;
        if (fs::exists(source, ec)) {
            collect(layout.workingDirectory(), source, settings.exclude, skipRoot, items);
        }
    }
    for (const auto& item : items) {
        needed += item.size;
    }
    if (items.empty()) {
        return std::unexpected("nothing to copy: none of the configured paths exist or all of them are excluded");
    }
    if (const auto space = fs::space(root, ec); !ec && space.available < needed + (needed / 10)) {
        return std::unexpected("not enough free space for the backup (" + std::to_string(needed) + " bytes needed)");
    }
    const std::string name = stamp(now) + "-" + cleanKind(kind);
    const fs::path partial = root / (name + (settings.compress ? ".zip" : "") + std::string(kPartial));
    const fs::path finalPath = root / (name + (settings.compress ? ".zip" : ""));
    if (fs::exists(finalPath, ec)) {
        return std::unexpected("a backup named " + name + " already exists");
    }
    BackupInfo info;
    const auto cancelled = [&] {
        fs::remove_all(partial, ec);
        return std::unexpected<std::string>("the backup was cancelled");
    };
    if (settings.compress) {
        const auto [dosTime, dosDate] = zip::dosStamp(now);
        auto writer = zip::Writer::create(partial, dosTime, dosDate);
        if (!writer) {
            return std::unexpected(writer.error());
        }
        for (const auto& item : items) {
            if (stop.stop_requested()) {
                return cancelled();
            }
            auto added = item.directory ? writer->addDirectory(item.relative, modeOf(item.source))
                                        : writer->addFile(item.relative, item.source, modeOf(item.source), stop);
            if (!added) {
                if (added.error() == zip::Skipped::kMessage) {
                    ++info.skipped;
                    continue;
                }
                fs::remove_all(partial, ec);
                return std::unexpected(added.error());
            }
            info.files += item.directory ? 0 : 1;
        }
        if (auto done = writer->finish(); !done) {
            fs::remove_all(partial, ec);
            return std::unexpected(done.error());
        }
    } else {
        fs::create_directories(partial, ec);
        for (const auto& item : items) {
            if (stop.stop_requested()) {
                return cancelled();
            }
            const fs::path target = partial / fs::path(item.relative);
            if (item.directory) {
                fs::create_directories(target, ec);
                continue;
            }
            fs::create_directories(target.parent_path(), ec);
            auto copied = copyChunked(item.source, target, stop);
            if (!copied) {
                if (copied.error() == zip::Skipped::kMessage) {
                    ++info.skipped;
                    continue;
                }
                fs::remove_all(partial, ec);
                return std::unexpected(copied.error());
            }
            ++info.files;
        }
    }
    fs::rename(partial, finalPath, ec);
    if (ec) {
        const std::string why = ec.message();
        fs::remove_all(partial, ec);
        return std::unexpected("cannot finish the backup: " + why);
    }
    const std::size_t files = info.files;
    const std::size_t skipped = info.skipped;
    info = describe(finalPath);
    info.files = files;
    info.skipped = skipped;
    auto all = listBackups(root);
    for (const auto& doomed : backupsToPrune(all, settings.retention, now)) {
        fs::remove_all(root / doomed, ec);
        fs::remove(root / (doomed + ".zip"), ec);
    }
    return info;
}

std::expected<BackupInfo, std::string> restoreBackup(const Layout& layout, const config::BackupConfig& settings,
                                                     std::string_view name, std::int64_t now) {
    const fs::path root = backupRoot(layout, settings);
    auto found = findBackup(root, name);
    if (!found) {
        return std::unexpected(found.error());
    }
    const fs::path staging = layout.workingDirectory() / ".hoist" / "restoring";
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    if (found->zipped) {
        if (auto extracted = zip::extract(found->path, staging); !extracted) {
            fs::remove_all(staging, ec);
            return std::unexpected(extracted.error());
        }
    } else {
        fs::copy(found->path, staging, fs::copy_options::recursive, ec);
        if (ec) {
            const std::string why = ec.message();
            fs::remove_all(staging, ec);
            return std::unexpected("cannot read the backup: " + why);
        }
    }
    config::BackupConfig safety = settings;
    safety.paths.clear();
    safety.exclude.clear();
    std::vector<fs::path> entries;
    for (const auto& entry : fs::directory_iterator(staging, ec)) {
        if (entry.path().filename() == ".hoist") {
            continue;
        }
        entries.push_back(entry.path());
        safety.paths.push_back(entry.path().filename().string());
    }
    if (auto copy = createBackup(layout, safety, kSafetyKind, now); !copy) {
        fs::remove_all(staging, ec);
        return std::unexpected("cannot take the safety backup, nothing was changed: " + copy.error());
    }
    const fs::path aside = layout.workingDirectory() / ".hoist" / "restoring-old";
    fs::remove_all(aside, ec);
    fs::create_directories(aside, ec);
    std::vector<fs::path> moved;
    for (const auto& entry : entries) {
        const fs::path target = layout.workingDirectory() / entry.filename();
        bool hadOld = false;
        if (fs::exists(target, ec)) {
            fs::rename(target, aside / entry.filename(), ec);
            hadOld = !ec;
        }
        if (!ec) {
            fs::rename(entry, target, ec);
        }
        if (ec) {
            const std::string why = ec.message();
            if (hadOld) {
                fs::rename(aside / entry.filename(), target, ec);
            }
            for (const auto& done : moved) {
                const fs::path back = layout.workingDirectory() / done.filename();
                fs::remove_all(back, ec);
                if (fs::exists(aside / done.filename(), ec)) {
                    fs::rename(aside / done.filename(), back, ec);
                }
            }
            fs::remove_all(staging, ec);
            fs::remove_all(aside, ec);
            return std::unexpected("cannot put " + target.string() + " back (" + why +
                                   "); what was there is untouched and a safety backup exists");
        }
        moved.push_back(entry);
    }
    fs::remove_all(aside, ec);
    fs::remove_all(staging, ec);
    return *found;
}

} // namespace basalt::hoist::install
