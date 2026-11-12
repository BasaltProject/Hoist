#!/usr/bin/env bash
# Usage: plan-version.sh <release|ci>
# Prints the version to stamp. Releases are <year>.<n>, one per commit to main.
set -euo pipefail

mode="${1:?mode}"
year="$(sed -n 's/^set(HOIST_VERSION_BASE "\([^"]*\)").*/\1/p' CMakeLists.txt)"
[ -n "$year" ] || { echo "no HOIST_VERSION_BASE in CMakeLists.txt" >&2; exit 1; }

latest=0
while read -r tag; do
    patch="${tag#v"$year".}"
    if [ "$patch" != "$tag" ] && [[ "$patch" =~ ^[0-9]+$ ]] && [ "$patch" -gt "$latest" ]; then
        latest="$patch"
    fi
done < <(gh release list --exclude-pre-releases --limit 100 --json tagName --jq '.[].tagName')

if [ "$mode" = release ]; then
    version="$year.$((latest + 1))"
else
    version="$year.$latest-ci${GITHUB_RUN_NUMBER:-0}"
fi
echo "$version"
[ -z "${GITHUB_OUTPUT:-}" ] || echo "version=$version" >>"$GITHUB_OUTPUT"
