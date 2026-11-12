<p align="center">
  <a href="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-white.svg#gh-dark-mode-only">
    <img src="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-white.svg" alt="Basalt" width="360">
  </a>
  <a href="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-black.svg#gh-light-mode-only">
    <img src="https://raw.githubusercontent.com/BasaltProject/.github/main/assets/basalt-logo-black.svg" alt="Basalt" width="360">
  </a>
</p>

<h2 align="center">Basalt Hoist</h2>

<p align="center">
  <strong>Keep Basalt Server and Proxy up to date automatically.</strong>
</p>

<p align="center">
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white">
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-macOS_%7C_Linux_%7C_Windows-00599C">
  <img alt="Build" src="https://img.shields.io/badge/build-CMake-064F8C?logo=cmake&logoColor=white">
  <img alt="Dependencies" src="https://img.shields.io/badge/deps-vcpkg-064F8C">
  <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-green">
</p>

## Overview

Basalt Hoist is a launcher for [Basalt Server](https://github.com/BasaltProject/Server) and
[Basalt Proxy](https://github.com/BasaltProject/Proxy). It starts the product as a child process, watches GitHub in
the background for a new `release` or `nightly`, and applies it the way you configured: at a daily restart, when a
condition you wrote is true, on demand, or straight away.

Basalt keeps old configurations working across versions, so updating should not be a gamble. Hoist makes it a safe
one: a download is checked against the release's `SHA256SUMS` before it is used, the product is stopped cleanly, the
previous version is kept, and a version that fails to start is rolled back by itself.

Like the rest of the Basalt Project it is written in **C++23** and ships as a native binary. It needs no JVM and calls
no other program on the host. **One folder holds one product and one Hoist.**

## Quick start

```bash
mkdir my-server && cd my-server
hoist init --target server        # or --target proxy; writes config/*.toml
hoist run                         # installs the latest release, starts it, keeps it up to date
```

Hoist only announces a new version in the log until you enable a way to apply it in `config/schedule.toml`.
The binary is named `BasaltHoist-<version>-<platform>-<arch>`; `hoist` is the same program under its short name.

| Command | What it does |
|---|---|
| `hoist run` | Start the product and keep it running (the default) |
| `hoist init` | Write the default configuration |
| `hoist check` | Look for a new version now and say what Hoist would do |
| `hoist status` | Show what is installed and how Hoist is set up |
| `hoist update` | Apply the pending update now (asks the running Hoist) |
| `hoist rollback` | Go back to the previous version |

Typing in Hoist's console goes to the product's console, so it stays usable. When the product stops by itself with
exit code 0 (you typed `stop`), Hoist stops too. Any other exit is a crash and is restarted with a limit.

## Configuration

Configuration lives in `config/`, one TOML file per domain, written commented on the first run.

| File | What it holds |
|---|---|
| `launcher.toml` | Which product, its command and arguments, how it is stopped and restarted after a crash |
| `updates.toml` | Channel (`release` or `nightly`), scope, check interval, download speed limit, backups, rollback grace |
| `schedule.toml` | When an update is applied: daily restart, immediate, and any number of conditions |
| `status.toml` | How Hoist reads the player count from the product (API with a token, or the server list ping) |

**Scope** says how far an update may go. `build` takes `26.3.3` to `26.3.4`; `minor` also takes `26.4.x`; `major` also
takes `27.x`. A version beyond the scope is announced and never installed. Hoist never goes back to an older version
on its own, except to undo an update that failed to start.

**Conditions** are expressions over placeholders, so you are not limited to a fixed set of rules. Each one can have its
own warning to the players and a grace time it must hold for:

```toml
[[conditions]]
name = "quiet night"
when = "%time% >= 03:00 && %time% < 06:00 && %players_online% < 5"
grace-seconds = 120
warning-seconds = 60
warning-command = "say Restarting in %restart_in% seconds to update"

[[conditions]]
name = "empty"
when = "%players_online% == 0"
```

The placeholders are `%time%`, `%hour%`, `%minute%`, `%weekday%`, `%players_online%`, `%players_max%`, `%tps%`,
`%uptime_minutes%`, `%update_available%`, `%installed_version%`, `%new_version%`, `%update_jump%`, `%channel%`. A
placeholder with no value makes the condition false, so nothing is ever applied blind. `schedule.toml` is reloaded
while Hoist runs.

## Building

You need CMake 3.28 or newer, Ninja, Clang and [vcpkg](https://github.com/microsoft/vcpkg). Dependencies are
declared in `vcpkg.json` and kept current by Dependabot. Hoist does not consume Wire.

```bash
git clone https://github.com/BasaltProject/Hoist.git
cd Hoist
cmake --preset release
cmake --build build/release
```

If `VCPKG_ROOT` is not set, a vcpkg checkout is looked for in `~/vcpkg` and otherwise cloned into the build folder.
The binary lands in `build/release/bin/`. `cmake --preset debug` builds the debug tree and `ctest --preset debug`
runs the tests; `asan` runs them under the sanitizers.

Examples for `systemd` and Docker are in [docs/examples](docs/examples).

## License

GPL-3.0-only. See [LICENSE](LICENSE).
