#include <basalt/hoist/config/Config.hpp>

namespace basalt::hoist::config {
namespace {

constexpr std::string_view kLauncher = R"(# What Hoist runs and how it keeps it running. One folder, one product.

# "server" or "proxy".
target = "@TARGET@"

# Let Hoist download and install the product. When false, Hoist only supervises `command`.
managed = true

# The program to run. Empty means the binary Hoist installed.
command = ""

# Arguments passed to the product. Hoist never accepts a license for you: if the product needs a flag for that,
# writing it here is your decision.
args = []

working-directory = "."

# Written to the product's console to stop it cleanly. Hoist waits for it to save and exit.
stop-command = "stop"
stop-timeout-seconds = 60

# After the timeout, end the process by force. Off means Hoist keeps waiting and says so in the log.
kill-after-timeout = true

# Start the product again when it stops on its own, after a pause.
restart-on-crash = true
restart-delay-seconds = 5

# Give up after this many crashes in a row. A run longer than stable-seconds resets the count.
max-consecutive-crashes = 5
stable-seconds = 300
)";

constexpr std::string_view kUpdates =
    R"(# How Hoist looks for new versions. Hoist only announces a version in the log until a mode in schedule.toml applies it.

# "release" follows the latest release. "nightly" follows the moving nightly build.
channel = "release"

# How far an update may go. A version beyond it is announced and never installed.
#   build: 26.3.3 to 26.3.4
#   minor: also 26.4.x
#   major: also 27.x
scope = "build"

# Minutes between checks. The check runs in the background and never touches the running product.
check-interval-minutes = 30

# Download a new version as soon as it is found, so applying it only needs a restart.
predownload = false

# Cap the download speed in KiB per second so a new version does not starve the players. 0 is unlimited.
download-limit-kib-per-second = 0

# Previous versions kept for rollback.
keep-previous = 2

# If the new version stops within this many seconds of starting, Hoist goes back to the previous one.
rollback-grace-seconds = 120

# Empty means BasaltProject/Server or BasaltProject/Proxy, following `target`.
repository = ""
api-url = "https://api.github.com"

# A GitHub token raises the rate limit. The environment variable HOIST_GITHUB_TOKEN is read when this is empty.
token = ""

# Empty means the platform this Hoist was built for, such as "Linux", "Linux-musl", "macOS" or "Windows".
asset-platform = ""

# A PEM file of trusted certificates. Empty means the host's own list.
ca-file = ""

# A copy of what an update could change, taken while the product is stopped.
[backup]
enabled = false
# Only for updates this big or bigger: build, minor or major.
min-scope = "minor"
# Paths relative to the working directory.
paths = ["worlds", "config", "players.db"]
destination = ".hoist/backups"
# Copies kept.
keep = 3
)";

constexpr std::string_view kStatus =
    R"(# How Hoist reads the player count and more from the running product, for the placeholders in schedule.toml.
# The API gives everything; the server list ping (the one a Minecraft client does) works with any server and only
# gives %players_online% and %players_max%. Without either, those placeholders have no value and a condition that
# needs them is never true.

# Seconds to wait for an answer.
timeout-seconds = 5

# The product's management API, with a token. Basalt Server enables it on its own, on 127.0.0.1:25580, and keeps the
# token in .basalt/remote-token. An existing install with the API off is not touched: Hoist says so in the log.
[api]
enabled = true
host = "127.0.0.1"
# 0 means the product's default port.
port = 0
# Empty means the product's default token file, relative to the working directory.
token-file = ""

[ping]
enabled = true
host = "127.0.0.1"
port = 25565
)";

constexpr std::string_view kSchedule =
    R"(# When a found update is applied. With nothing enabled here, update by hand with `hoist update`.
# Any enabled mode that fires applies the update; the first one that does wins.
#
# A condition is an expression over placeholders written between percent signs:
#   %time%             minutes since midnight, to compare with a clock such as 03:00
#   %hour% %minute% %weekday% (1 Monday to 7 Sunday) %day% %month% %year%
#   %players_online% %players_max%        from the product's API, or from its server list ping
#   %uptime_minutes%   how long the product has been running
#   %update_available% %installed_version% %new_version% %update_jump% (build, minor or major) %channel%
# Operators: && || ! == != < <= > >= + - * / and parentheses. Text goes in 'quotes'.
# A placeholder with no value makes the expression false: nothing is applied blind.

# "local", "UTC" or an IANA name such as "Europe/Madrid".
timezone = "local"

[daily-restart]
enabled = false
time = "04:00"
# Tell the players this many seconds ahead through the product's console. 0 means no warning.
warning-seconds = 60
# The console command for the warning. The placeholder %restart_in% is the seconds left.
warning-command = "say Restarting in %restart_in% seconds to update"

[immediate]
enabled = false
warning-seconds = 60
warning-command = "say Restarting in %restart_in% seconds to update"

# Any number of conditions. Remove the # to use one.
# [[conditions]]
# name = "quiet night"
# when = "%time% >= 03:00 && %time% < 06:00 && %players_online% < 5"
# grace-seconds = 120
# warning-seconds = 60
# warning-command = "say Restarting in %restart_in% seconds to update"
#
# [[conditions]]
# name = "empty"
# when = "%players_online% == 0"
)";

std::string withTarget(std::string_view text, Target target) {
    std::string out = "format = " + std::to_string(kFormat) + "\n";
    out += text;
    if (auto at = out.find("@TARGET@"); at != std::string::npos) {
        out.replace(at, 8, nameOf(target));
    }
    return out;
}

} // namespace

std::vector<std::pair<std::string, std::string>> defaultFiles(Target target) {
    return {{"launcher.toml", withTarget(kLauncher, target)},
            {"updates.toml", withTarget(kUpdates, target)},
            {"schedule.toml", withTarget(kSchedule, target)},
            {"status.toml", withTarget(kStatus, target)}};
}

} // namespace basalt::hoist::config
