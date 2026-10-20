# Building and developing

## Build

Requires a C++20 compiler, SDL3 (`sdl3` package), CMake >= 3.24 and Ninja. nlohmann/json is used from the system if installed
(`nlohmann-json` package), else fetched at configure time. `glslc` (the `shaderc` package) is optional, see [the GPU CRT filter](CONFIGURATION.md#gpu-crt-filter). The game reads `data/` from the source tree it was built in (`LABYTERM_DATA` overrides),
including the original's town, title and boss lair pictures (`data/art`); nothing is read from the original Labyrinth at run time.

    tools/build.sh                                   # configure (Release) and build into build/
    cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DLABYTERM_DEBUG_KEYS=OFF   # a ship build: no debug hotkeys
    ctest --test-dir build                           # tests

Other entry points:
`labyterm --map 7` (dungeon viewer), `--layouts 4` (layout mocks), `--fonts` (font and glyph lab), `--tuning` (list all knobs),
`--check-data`, `--smoke`. Headless pictures need no window: `build/walk_dump <seed> <moves> out.png` (see `CLAUDE.md`).
Without a display or with broken data the game prints a one-line error and exits with status 1.

To install it for your user (command on `PATH` plus a start-menu entry; the binary finds `../share/labyterm` by itself):

    cmake --install build-release --prefix ~/.local  # ~/.local/bin/labyterm, ~/.local/share/applications/labyterm.desktop

## Source layout

    src/render/    CellGrid (the game's framebuffer), Backend interface and SDL3 window backend, glyph atlas,
                   CPU frame renderer + CRT filter, pixel art drawn in text cells
    third_party/   vendored stb headers (MIT / public domain)
    src/audio/     Cues (what the game reports), Mixer, AudioPlayer (SDL3 audio device)
    src/platform/  Omarchy theme loader
    src/core/      JSON data loading (data/ dir, override with $LABYTERM_DATA), generated Enums.hpp, tuning
    data/          All game data as JSON (equipment, monsters, ...) + fonts/
    data/art/      Monster art (2x, two-tone PNGs: R = tone, A = alpha); edit these files directly
    tools/         Build, balance-report, tuning-codegen and screenshot scripts (see tools/README.md)
    tests/         Unit tests (ctest), balance bots, simulators and inspection tools (see tests/README.md)

Contributor notes (conventions, pitfalls, the debug hotkeys, how the art is previewed) are in `CLAUDE.md`.
