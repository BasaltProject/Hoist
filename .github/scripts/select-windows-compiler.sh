#!/usr/bin/env bash
set -euo pipefail

llvm_major() { clang --version 2>/dev/null | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1; }
arm64=0
if [ "${PROCESSOR_ARCHITECTURE:-}" = ARM64 ] || [ "${PROCESSOR_ARCHITEW6432:-}" = ARM64 ]; then
    arm64=1
fi
# The MSVC STL on the runner image rejects a Clang older than 22.
major="$(llvm_major)"
if [ "${major:-0}" -lt 22 ]; then
    choco upgrade llvm --no-progress -y || true
    export PATH="/c/Program Files/LLVM/bin:$PATH"
    echo "C:\\Program Files\\LLVM\\bin" >>"$GITHUB_PATH"
    major="$(llvm_major)"
fi
if [ "${major:-0}" -lt 22 ]; then
    # Chocolatey answers 503 now and then and still exits 0, so take the installer from the LLVM release.
    version=22.1.8
    installer="LLVM-$version-win64.exe"
    if [ "$arm64" = 1 ]; then
        installer="LLVM-$version-woa64.exe"
    fi
    curl -fsSL --proto "=https" --tlsv1.2 --retry 5 --retry-all-errors --retry-delay 10 -o "$RUNNER_TEMP/llvm.exe" \
        "https://github.com/llvm/llvm-project/releases/download/llvmorg-$version/$installer"
    powershell -NoProfile -Command "Start-Process -FilePath '$(cygpath -w "$RUNNER_TEMP/llvm.exe")' -ArgumentList '/S','/D=C:\\LLVM' -Wait"
    export PATH="/c/LLVM/bin:$PATH"
    echo "C:\\LLVM\\bin" >>"$GITHUB_PATH"
    major="$(llvm_major)"
fi
if [ "${major:-0}" -lt 22 ]; then
    echo "Clang ${major:-none} is older than 22 and could not be upgraded" >&2
    exit 1
fi
if ! command -v ninja >/dev/null; then
    choco install ninja --no-progress -y
fi
