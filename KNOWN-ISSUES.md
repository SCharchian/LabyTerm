# Known issues and notes for other Linux distributions

LabyTerm is developed on Omarchy (Arch Linux, Hyprland, Wayland). It is built on SDL3 and should run on any Linux with SDL3, but only Omarchy is tested. This page lists what is known to be Omarchy-specific or untested, as a starting point for anyone porting it. Fixes are welcome.

## What is tested

- Omarchy (Arch, Hyprland, Wayland, Vulkan GPU): everything.
- Everything else: not tested. Treat the points below as expectations, not facts.

## Build

- Needs a C++20 compiler, CMake 3.24 or newer, Ninja (or Make), and **SDL3**. SDL3 is new: older releases of Debian, Ubuntu and Fedora may not package it. Build SDL3 from source in that case.
- nlohmann/json is taken from the system if installed (the `nlohmann-json` package), otherwise downloaded at configure time (network needed): the v3.12.0 release tarball, checked against a SHA-256 in `CMakeLists.txt`.
- `glslc` (the `shaderc` package) is optional. Without it, or without Vulkan, the screen filter falls back to the CPU version, which is much slower at large window sizes (about 110 ms per frame at 1440p fullscreen, against about 2.6 ms on the GPU). Use filter level 0 to turn it off if it feels slow.
- The DEBUG hotkeys are off in a normal build. They are compiled in only for a Debug build type, or with `-DLABYTERM_DEBUG_KEYS=ON` (what `tools/build.sh` passes).
- The data folder is found next to the binary (`../share/labyterm`) or in the source tree; `LABYTERM_DATA` overrides.

## Omarchy-specific parts

- **Theme colors** come from `~/.local/state/omarchy/current/theme/colors.toml`. Elsewhere the game uses a built-in green-on-black palette. `LABYTERM_THEME_DIR` points at a folder with a `colors.toml` and `theme.name` to use another theme. TODO: read other desktops' palettes or a plain config file.
- **The Omarchy plugin** (`packaging/omarchy-plugin/`: launcher entry, bar widget, drop-down window) needs Omarchy, Hyprland (`hyprctl`, `jq`), `xdg-terminal-exec`, and pacman. It does not work on other setups, and is not needed to play.
- **The drop-down takes along windows opened while it is showing.** It lives on a Hyprland special workspace, and a new window opens on the active workspace, so an app you start while the drop-down is up hides and shows with it (Omarchy's scratchpad does the same). Hide the drop-down first. TODO: have the plugin move other apps' windows off its workspace.
- `labyterm-launch` installs build dependencies with `sudo pacman`. TODO: detect apt and dnf and print the package names instead.
- The default font is Adwaita Mono and VT323 is included; both ship in `data/fonts`, so no system font is required (the glyph atlas also looks for a few common system monospace fonts).

## Window and display

- `LABYTERM_SNAP=1` (resize to whole cell sizes) relies on a Wayland compositor that honors window size limits. Untested on GNOME, KDE and X11; it is off by default.
- The window opens at 66x44 cells of the chosen size (`LABYTERM_CELL`). On small screens or high scaling, pick a smaller cell size.
- Hidden or unfocused windows pause audio and the torch flicker. Compositors that do not report focus changes (some X11 setups) may keep them running.
- Fractional scaling and multi-monitor setups with different scales are untested.

## Audio

- Uses the SDL3 audio device. With no device, the game runs silently. PipeWire and PulseAudio are expected to work; plain ALSA is untested.

## Files and paths

- Settings: `~/.config/labyterm/settings.json`, tuning: `~/.config/labyterm/tuning.json`, saves and scores: `~/.local/state/labyterm/` (`LABYTERM_STATE_DIR`). `XDG_CONFIG_HOME` and `XDG_STATE_HOME` are honored.
- Saves are single-use and written only from the town sign post. Copying the file is the only backup.
- There is one save slot, shared by every copy of the game on your account (the plugin's drop-down and a launcher start use the same file, so a run can be saved in one and resumed in the other). If two copies run at the same time, whichever saves last overwrites the other's save, and loading a save uses it up for both.

## Packaging

- No package exists for Debian, Fedora, Nix, Flatpak or AppImage. A Flatpak needs the SDL3 runtime and the state/config paths mapped.
- The install target puts the binary, data, desktop entry, icons and licenses in a prefix (`cmake --install build --prefix /usr`); a distro package should use that.
