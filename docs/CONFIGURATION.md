# Configuration

Everything here is optional: the game runs with its defaults and nothing is installed into your environment.

## Saves and settings

- **Save**: only at the town sign post ("Save and Exit"). There is no autosave, a save works once (loading it uses it up), and dying
  ends the run: your name and best item come back as a ghost on the floor where you died, and good scores enter the Legends.
  State lives in `~/.local/state/labyterm/` (`save.json`, `graveyard.json`, `scores.json`; `LABYTERM_STATE_DIR` overrides).
- **Settings** (volumes, theme, CRT look, key repeat, key bindings) are kept in `~/.config/labyterm/settings.json`. A damaged file is
  ignored field by field. Everything in it can be changed from the Options menu.

## Theme colors

The UI colors follow your Omarchy theme live (`~/.local/state/omarchy/current/theme/colors.toml`); with no theme present the game uses a
built-in green-on-black palette. If a theme looks garish, tone it down in your tuning file under `display`: `themeColors` (0 = ignore the theme),
`themeStrength` (blend with the built-in palette), `themeSaturation` (colorfulness) and `frameMix` (brightness of the panel outlines).
`./build/labyterm --layouts 4` previews the main screen (T toggles theme colors, L cycles line styles).

## Sound

The original's sound effects and music play (WAV files in `data/audio/`;
`data/audio.json` maps cues to files). `A` cycles Full Audio, Effects Only and Full Mute (the Music and Effects switches in Options). Volumes are in Options (and the `audio` knobs in the tuning file). With no sound device the game runs silently. `ctest` runs `audio_test` (every file decodes, mixer, cues).

## Tuning

All balance numbers live in `data/gameplay.json` (every knob has a comment). To experiment, put just the
values you want in `~/.config/labyterm/tuning.json`, e.g. `{ "combat": { "baseToHit": 0.55 } }`; the game
reloads it while running; a file with a typo or bad JSON is rejected (the last good values stay) and the reason is printed to stderr. `./build/labyterm --tuning` lists all knobs.
After adding/removing a knob in `gameplay.json`, run `python3 tools/gen_tuning.py` and rebuild.
`data/*.json` is hand-tuned data: edit it directly.

## Environment variables

All optional; the game runs without any of them, and nothing installs them. The Omarchy plugin sets `LABYTERM_CELL`, `LABYTERM_MARGIN` and `LABYTERM_SNAP` for the process it launches (see `packaging/omarchy-plugin/README.md`).

| Variable | What it does |
|---|---|
| `LABYTERM_CELL=N` | Starting cell height in pixels (even, 8 to 96); the window opens at 66N x 44N. |
| `LABYTERM_MARGIN=N` | Logical pixels (0 to 64, default 0) of background kept around the grid. |
| `LABYTERM_SNAP=1` | After every resize, ask the compositor for exactly the grid at the whole cell size that fits (no dead space). Wayland compositors that honor size limits only. |
| `LABYTERM_CRT=0..3` | Screen filter level (default 2); overrides the saved setting. |
| `LABYTERM_CPU_CRT=1` | Use the CPU version of the filter even if a GPU one is available. |
| `LABYTERM_TEXT=6..12` | Text size in sixths of the cell size (6 = same as the cell). |
| `LABYTERM_TUNING` | Path of the tuning file (default `~/.config/labyterm/tuning.json`). |
| `LABYTERM_SETTINGS` | Path of the saved-settings file (default `~/.config/labyterm/settings.json`). |
| `LABYTERM_STATE_DIR` | Folder for save, ghost and scores (default `~/.local/state/labyterm`). |
| `LABYTERM_DATA` | Folder holding the game data (default: next to the executable, else the source tree). |
| `LABYTERM_THEME_DIR` | Folder with `colors.toml` and `theme.name` that replaces the Omarchy theme. |
| `LABYTERM_DEBUG=1` | Print window sizing and frame timings to stderr. |
| `LABYTERM_SKIP_TITLE=1` | Start straight in a dungeon (for measuring). |

`LABYTERM_DEBUG_KEYS` is a CMake option, not an environment variable: release builds use `-DLABYTERM_DEBUG_KEYS=OFF`.

## GPU CRT filter

The CRT look (beam spread, glow, raster lines, vignette) runs as fragment shaders on the GPU via SDL_GPU (Vulkan/SPIR-V),
about 2.6 ms instead of ~110 ms per frame at 1440p fullscreen. The shaders are in `src/render/shaders/` and are compiled with
`glslc` at build time and embedded; without `glslc`, or without a usable GPU, the game falls back to the CPU filter
(`applyCrt`), which `LABYTERM_CPU_CRT=1` also forces. `build/gpu_crt_test [cellH] [crt] [png prefix]` compares the two
headlessly and times them.

## Idle cost

LabyTerm is meant to run all day, so an idle window costs about 0% CPU (it sleeps until input, a window event or a
once-a-second file check). Check with `tools/measure_cpu.sh 10 -- ./build/labyterm`. The screen look
(glow, scanlines, ...) is live-tunable under `display` in your tuning file.
