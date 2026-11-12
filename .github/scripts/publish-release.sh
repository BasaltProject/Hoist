#!/usr/bin/env bash
# Usage: publish-release.sh <version> <sha> <artifact dir>
# Publishes the downloaded binaries as they are, with a SHA256SUMS next to them.
set -euo pipefail

version="${1:?version}"
sha="${2:?sha}"
artifacts="${3:?artifact dir}"
out="$(mktemp -d)"

cp "$artifacts"/*/* "$out"/
(cd "$out" && sha256sum -- * >SHA256SUMS)
gh release create "v$version" "$out"/* --target "$sha" --title "$version" --generate-notes --latest
