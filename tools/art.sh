#!/usr/bin/env bash
# Art workbench: rebuild and render a contact sheet of the procedural art.
#   tools/art.sh [set] [scale=4] [ss=2] [crt=0]   set = all | elevator | treasure | chest | lair | close | zoom | side | floor. Drawn through the game's own path; crt 1-3 adds its CRT filter.
#   tools/art.sh watch [set] [scale] [ss]  redo it every time src/ui/CorridorRenderer.cpp is saved
# Output: build/art/<set>.png, opened in imv (once; imv reloads it on every re-render). NOVIEW=1 skips opening it.
set -euo pipefail
cd "$(dirname "$0")/.."
for d in "$HOME"/.local/share/mise/installs/cmake/latest/*/bin "$HOME"/.local/share/mise/installs/ninja/latest; do
  [[ -d "$d" ]] && PATH="$d:$PATH"  # mise shims need a per-directory version; use the installs directly
done
export PATH

render() {
  local set="${1:-elevator}" scale="${2:-4}" ss="${3:-2}" crt="${4:-0}"
  mkdir -p build/art
  cmake --build build --target art_sheet 2>&1 | grep -E "error|warning: unused|FAILED" || true
  ./build/art_sheet "build/art/$set.png" "$set" "$scale" "$ss" "$crt"
  echo "build/art/$set.png"
  view "$PWD/build/art/$set.png"
}

# Open the picture in imv unless one is already showing it (imv reloads a file when it changes). NOVIEW=1 turns this off.
view() {
  [[ -z "${NOVIEW:-}" ]] || return 0
  command -v imv > /dev/null || return 0
  pgrep -f "^(/usr/bin/)?imv $1\$" > /dev/null && return 0
  setsid -f imv "$1" > /dev/null 2>&1 < /dev/null
}

if [[ "${1:-}" == "watch" ]]; then
  shift
  last=""
  while true; do
    cur="$(stat -c %Y src/ui/CorridorRenderer.cpp)"
    if [[ "$cur" != "$last" ]]; then last="$cur"; render "$@" || true; fi
    sleep 0.5
  done
else
  render "$@"
fi
