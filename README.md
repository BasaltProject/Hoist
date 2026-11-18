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
  <strong>Keeps Basalt Server and Proxy up to date automatically.</strong>
</p>

<p align="center">
  <img alt="C++" src="https://img.shields.io/badge/language-C%2B%2B-00599C?logo=cplusplus&logoColor=white">
  <img alt="Platforms" src="https://img.shields.io/badge/platforms-macOS_%7C_Linux_%7C_Windows-00599C">
  <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-green">
</p>

## About

Hoist is a launcher for [Basalt Server](https://github.com/BasaltProject/Server) and
[Basalt Proxy](https://github.com/BasaltProject/Proxy). It runs the product as a child process, watches GitHub for a
new `release` or `nightly`, and applies it the way you configured: at a daily restart, when a condition you wrote is
true, on demand, or straight away.

Updates are safe: a download is checked against `SHA256SUMS`, the product is stopped cleanly, the previous version is
kept, and a version that fails to start is rolled back by itself. Hoist also takes scheduled and pre-update backups.
One folder holds one product and one Hoist.

## Quick start

```bash
mkdir my-server && cd my-server
hoist init --target server        # or --target proxy
hoist run                         # installs the latest release, starts it, keeps it up to date
```

Hoist only announces a new version until you enable a way to apply it in `config/hoist/schedule.toml`.

| Command | What it does |
|---|---|
| `hoist run` | Start the product and keep it running |
| `hoist check` | Look for a new version now |
| `hoist status` | Show what is installed |
| `hoist update` | Apply the pending update now |
| `hoist rollback` | Go back to the previous version |
| `hoist backup` | Create, list, restore and delete backups |

## Building

```bash
git clone https://github.com/BasaltProject/Hoist.git
cd Hoist
cmake --preset release
cmake --build build/release
```

Needs CMake 3.28+, Ninja, Clang and [vcpkg](https://github.com/microsoft/vcpkg). Examples for `systemd` and Docker are
in [docs/examples](docs/examples).

## License

[GPL-3.0-only](LICENSE).
