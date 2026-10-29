#!/usr/bin/env bash
# Writes Ghostty's current colors as a LabyTerm theme (colors.toml + theme.name), for desktops without an Omarchy theme.
#   tools/ghostty_theme.sh [dir]    default: ${XDG_CONFIG_HOME:-~/.config}/labyterm/theme
# The game reads the default folder by itself when there is no Omarchy theme; another folder needs LABYTERM_THEME_DIR=<dir>.
# Run it again after changing Ghostty's theme: a running game picks up the new colors within a second (it watches the
# files' modification times). Delete the folder to go back to the built-in palette.
set -euo pipefail
command -v ghostty > /dev/null || { echo "ghostty_theme.sh: ghostty is not on PATH" >&2; exit 1; }
default="${XDG_CONFIG_HOME:-$HOME/.config}/labyterm/theme"
dir="${1:-$default}"
mkdir -p "$dir"

# `ghostty +show-config --changes-only=false` prints the effective config, defaults included, with the theme's colors
# already resolved (without the flag it prints only what differs from Ghostty's defaults). The game reads flat
# `key = "#rrggbb"` lines; background, foreground, selection and color0..color15 are enough: the other slots (accent,
# red, muted, ...) fall back to the ANSI colors.
config="$(ghostty +show-config --changes-only=false)"
colors="$(awk -F ' = ' '
  function hex(v) { if (v ~ /^#?[0-9a-fA-F]{6}$/) return (v ~ /^#/ ? v : "#" v); return "" }
  ($1 == "background" || $1 == "foreground") && hex($2) != "" { printf "%s = \"%s\"\n", $1, hex($2) }
  $1 == "selection-background" && hex($2) != ""              { printf "selection = \"%s\"\n", hex($2) }
  $1 == "palette" { split($2, p, "="); if (p[1] + 0 < 16 && hex(p[2]) != "") printf "color%d = \"%s\"\n", p[1], hex(p[2]) }
' <<< "$config")"
[[ -n "$colors" ]] || { echo "ghostty_theme.sh: found no colors in 'ghostty +show-config'" >&2; exit 1; }
name="$(awk -F ' = ' '$1 == "theme" { print $2 }' <<< "$config")"

# Write to a temporary file and rename, so a running game never reads half a file.
printf '%s\n' "$colors" > "$dir/colors.toml.tmp" && mv "$dir/colors.toml.tmp" "$dir/colors.toml"
printf '%s\n' "Ghostty${name:+ ($name)}" > "$dir/theme.name.tmp" && mv "$dir/theme.name.tmp" "$dir/theme.name"
echo "Wrote $dir/colors.toml."
# Say when the game will not read this folder: the variable points elsewhere, an Omarchy theme comes first, or a custom folder.
omarchy="$HOME/.local/state/omarchy/current/theme/colors.toml"
if [[ -n "${LABYTERM_THEME_DIR:-}" && "$LABYTERM_THEME_DIR" != "$dir" ]]; then
  echo "Note: LABYTERM_THEME_DIR is set to $LABYTERM_THEME_DIR, so the game reads that folder instead. Unset it to use this one."
elif [[ -z "${LABYTERM_THEME_DIR:-}" && "$dir" != "$default" ]]; then
  echo "Start the game with LABYTERM_THEME_DIR=$dir to use it."
elif [[ -z "${LABYTERM_THEME_DIR:-}" && -f "$omarchy" ]]; then
  echo "Note: the Omarchy theme comes first; start the game with LABYTERM_THEME_DIR=$dir to use this one."
else
  echo "The game uses it from now on (a running game within a second)."
fi
