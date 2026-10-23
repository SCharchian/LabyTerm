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

# The plugin builds exactly the commit PIN in its launcher (the variable and the literal in the checkout line must agree);
# once the release tag exists, that must be the tag's commit. Before the tag exists the pin cannot be checked yet.
launch=packaging/omarchy-plugin/labyterm-launch
pins=$(grep -o '[0-9a-f]\{40\}' "$launch" | sort -u)
if [[ $(printf '%s\n' "$pins" | grep -c .) != 1 ]]; then echo "PIN MISMATCH: $launch must contain exactly one distinct 40-character commit (found: $(echo $pins))" >&2; exit 1; fi
if [[ $(grep -c "$pins" "$launch") -lt 2 ]]; then echo "PIN MISMATCH: the commit must appear in PIN= and in the checkout line of $launch" >&2; exit 1; fi
if tag_commit=$(git rev-parse -q --verify "refs/tags/v$cmake_v^{commit}" 2>/dev/null); then
  if [[ $tag_commit == "$pins" ]]; then echo "launcher pin = commit of v$cmake_v (${pins:0:10})"; else echo "PIN MISMATCH: launcher pins $pins but v$cmake_v is $tag_commit" >&2; exit 1; fi
else
  echo "note: tag v$cmake_v does not exist yet; after tagging, set PIN in $launch to \$(git rev-parse v$cmake_v^{commit})"
fi
