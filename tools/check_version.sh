#!/usr/bin/env bash
# Fails when the version in CMakeLists.txt, the Omarchy manifest, the PKGBUILD and the top CHANGELOG entry differ.
#   usage: tools/check_version.sh
set -euo pipefail
cd "$(dirname "$0")/.."
cmake_v=$(sed -n 's/^project(LabyTerm VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)
manifest_v=$(sed -n 's/.*"version": "\([^"]*\)".*/\1/p' packaging/omarchy-plugin/manifest.json)
pkg_v=$(sed -n 's/^pkgver=\(.*\)/\1/p' packaging/arch/PKGBUILD)
log_v=$(sed -n 's/^## \[\([0-9][0-9.]*\)\].*/\1/p' CHANGELOG.md | head -1)
printf 'CMakeLists.txt %s\nmanifest.json  %s\nPKGBUILD       %s\nCHANGELOG.md   %s\n' "$cmake_v" "$manifest_v" "$pkg_v" "$log_v"
if [[ -n $cmake_v && $cmake_v == "$manifest_v" && $cmake_v == "$pkg_v" && $cmake_v == "$log_v" ]]; then echo "versions agree: $cmake_v"; else echo "VERSION MISMATCH" >&2; exit 1; fi
