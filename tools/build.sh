#!/usr/bin/env bash
# Rebuild the game and tools:  tools/build.sh [cmake --build args, e.g. --target labyterm]
# Uses the cmake/ninja that mise has installed directly, so it works even when the `cmake` shim has no version set.
set -euo pipefail
cd "$(dirname "$0")/.."
for d in "$HOME"/.local/share/mise/installs/cmake/latest/*/bin "$HOME"/.local/share/mise/installs/ninja/latest; do
  [[ -d "$d" ]] && PATH="$d:$PATH"
done
export PATH
# A development build: the DEBUG hotkeys are compiled in (they are off by default, for players and packages).
[[ -f build/CMakeCache.txt ]] || cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLABYTERM_DEBUG_KEYS=ON -G Ninja
exec cmake --build build "$@"
