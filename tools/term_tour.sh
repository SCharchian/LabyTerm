#!/usr/bin/env bash
# Renders a tour of the game's screens as the terminal build lays them out, and builds
# contact sheets, so the whole UI can be looked over without playing.
#   tools/term_tour.sh [COLSxROWS=100x30] [outdir=build/tour/<size>]
# Needs build/walk_dump and ImageMagick (montage). Each scenario is "name|moves|extra env"; the moves are walk_dump's.
set -euo pipefail
cd "$(dirname "$0")/.."
SIZE=${1:-100x30}
OUT=${2:-build/tour/$SIZE}
mkdir -p "$OUT"
rm -f "$OUT"/*.png
SCENES=(
  "01_title|H|"
  "02_creation|HE|"
  "03_legends|HDE|"
  "04_gameover|FX|"
  "05_walk|UU|"
  "06_walk_turn|ULU|"
  "07_minimap_corner|UUUU|"
  "08_fight_knight|FW|WALK_MONSTER=Knight"
  "09_fight_bat|FW|WALK_MONSTER=Bat"
  "10_fight_drake|FW|WALK_MONSTER=Drake"
  "11_fight_lich|FW|WALK_MONSTER=Lich"
  "12_fight_menu_scrolls|GFWs|"
  "13_boss_lair|B|"
  "14_boss_lair_step|BU|"
  "15_boss_fight|BUW|"
  "16_chest_closed|T|"
  "17_chest_open|TUE|"
  "18_chest_potion|JE|"
  "19_chest_scroll|CE|"
  "20_secret_door|K|"
  "21_secret_offer|KUUUE|"
  "22_elevator|V|"
  "23_town_sign|O|"
  "24_town_sign_5|ODDDD|"
  "25_town_temple|OE|"
  "26_town_smith|OGDE|"
  "27_town_smith_detail|OGDEE|"
  "28_town_beast|OGDDE|"
  "29_town_beast_sell|OGDDEE|"
  "30_sysmenu|Q|"
  "31_sys_options|QE|"
  "32_sys_help|QDE|"
  "33_sys_about|QDDE|"
  "34_inventory|Gi|"
  "35_scrolls|Gs|"
  "36_map|Um|"
  "37_portal|I|"
  "38_levelup|Z|"
  "39_perk|ZZZ|"
  "40_hurt_low_hp|FWX|"
  "41_boss10_lair|VDDDDDDDDDEB|"
  "42_boss10_fight|VDDDDDDDDDEBUW|"
)
for sc in "${SCENES[@]}"; do
  IFS='|' read -r name moves envs <<<"$sc"
  # shellcheck disable=SC2086
  env WALK_GRID="$SIZE" WALK_LABEL="$name" $envs build/walk_dump 7 "$moves" "$OUT/$name.png" 16 0 >/dev/null 2>&1 || echo "FAILED: $name ($moves)"
done
# contact sheets: four screens per sheet, in order
ls "$OUT"/[0-9]*.png | xargs -n4 | nl -w2 -n rz | while read -r n a b c d; do
  montage $a $b $c $d -tile 2x2 -geometry +6+6 -background '#202020' "$OUT/sheet_$n.png" 2>/dev/null || true
done
echo "tour in $OUT: $(ls "$OUT"/[0-9]*.png | wc -l) screens, $(ls "$OUT"/sheet_*.png 2>/dev/null | wc -l) sheets"
