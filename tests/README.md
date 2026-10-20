# tests/

Everything here is built from the top-level `CMakeLists.txt`, and none of it ships with the game or the packages (they build only `--target labyterm`).

    tools/build.sh                       # builds the game, the tests and the tools
    ctest --test-dir build               # runs the checks below

## Unit tests (run by `ctest`)

| File | What it checks |
|---|---|
| `level_test`, `corridor_test` | the level generator and the corridor renderer |
| `combat_sim`, `monster_action_test`, `scroll_test` | combat, monster specials and ailments, scroll casting |
| `fountain_test`, `secret_test`, `store_test`, `loot_rules_test` | fountains, the secret door, the town shops, loot |
| `save_test`, `floor_persist_test`, `persist_fuzz_test`, `run_test` | saves, floors that stay the same dungeon, hostile state files, the run loop |
| `menu_test`, `audio_test`, `text_shapes_test`, `text_glow_test` | key bindings and menus, sound, the Text dungeon style |
| `balance_rules_test` | the balance rules decided with the author |
| `session_sim` | a random bot plays many headless sessions to shake out crashes (uses `session_force.json`) |

## Balance bot (not in `ctest`, takes minutes)

`balance_bot.cpp` plays complete runs on the real game rules and writes CSV tables. `tools/balance_report.py` runs many of them in parallel and writes `build/balance/REPORT.md`:

    tools/build.sh --target balance_bot
    tools/balance_report.py --seeds 5000

## Inspection and benchmark tools (headless, write PNGs or numbers)

`art_sheet`, `atlas_dump`, `minimap_dump`, `scroll_dump`, `variant_dump`, `view_dump`, `walk_dump`, `ride_sheet`, `text_charset_pick` (pictures), `draw_bench`, `gpu_crt_test` (speed and parity). `png_grid.hpp` is their shared helper. Most files start with a comment that shows their usage.
