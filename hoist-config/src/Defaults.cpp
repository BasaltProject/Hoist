#include <basalt/hoist/config/Config.hpp>
#include <basalt/hoist/config/ConfigDefaults.hpp>

namespace basalt::hoist::config {
namespace {

std::string withTarget(std::string_view text, Target target) {
    std::string out(text);
    if (auto at = out.find("@TARGET@"); at != std::string::npos) {
        out.replace(at, 8, nameOf(target));
    }
    return out;
}

} // namespace

std::vector<std::pair<std::string, std::string>> defaultFiles(Target target) {
    return {{"launcher.toml", withTarget(kLauncherDefaultToml, target)},
            {"updates.toml", withTarget(kUpdatesDefaultToml, target)},
            {"schedule.toml", withTarget(kScheduleDefaultToml, target)},
            {"status.toml", withTarget(kStatusDefaultToml, target)},
            {"backup.toml", withTarget(kBackupDefaultToml, target)}};
}

} // namespace basalt::hoist::config
