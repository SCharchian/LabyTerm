#!/usr/bin/env bash
# Renders two screenshots per Omarchy theme (the start screen and a dungeon view with a chest) into build/theme_shots/<theme>-{start,dungeon}.png,
# through the game's own headless path, one theme at a time via $LABYTERM_THEME_DIR.
#   tools/theme_shots.sh [theme ...]    default: every theme in /usr/share/omarchy/themes and ~/.config/omarchy/themes
# Needs build/walk_dump (tools/build.sh --target walk_dump). Env: SEED (default 7), CELLH (default 14).
set -euo pipefail
cd "$(dirname "$0")/.."
out=build/theme_shots
mkdir -p "$out"
seed="${SEED:-7}" cellh="${CELLH:-14}"
dirs=()
if [[ $# -gt 0 ]]; then
  for n in "$@"; do
    for base in "$HOME/.config/omarchy/themes" /usr/share/omarchy/themes; do [[ -d "$base/$n" ]] && { dirs+=("$base/$n"); break; }; done
  done
else
  for base in /usr/share/omarchy/themes "$HOME/.config/omarchy/themes"; do
    for d in "$base"/*/; do [[ -f "$d/colors.toml" ]] && dirs+=("${d%/}"); done
  done
fi
for d in "${dirs[@]}"; do
  n="$(basename "$d")"
  export LABYTERM_THEME_DIR="$d"
  ./build/walk_dump "$seed" H "$out/$n-start.png" "$cellh" 0 > /dev/null
  ./build/walk_dump "$seed" T "$out/$n-dungeon.png" "$cellh" 0 > /dev/null
  echo "$n"
done
