# Changelog

All notable changes to LabyTerm. Versions follow [Semantic Versioning](https://semver.org/):
saves and settings stay compatible within a major version.

## [1.1.0] - unreleased

Play in your terminal
- `labyterm -t` (or `--terminal`) plays the whole game inside a truecolor terminal, drawn with Unicode octant block characters, themed like the window. `labyterm -w` (or `--window`) plays in the window. With neither, a window opens when there is a display, and the terminal is used when there is none (over ssh). `labyterm --help` lists the options.
- The terminal version adapts to the size of its window (at least 100 x 30 cells): the town, the shops, the scroll and orb rewards, the elevator and the fights all have their own layouts for it. The start screen says how to play the other way until you have tried both.
- The HP bar is red and the MP bar blue in the terminal, damage shows as a pale lost slice of the bar, and the health and mana orbs have their own colors.

Also
- Debug hotkeys are off in release builds, the elevator ride keeps its music while the shaft slides, and the build fetches nlohmann/json as a checked tarball.

## [1.0.0] - 2026-10-21

First release. LabyTerm is a first-person dungeon crawler drawn as colored text cells, themed from your Omarchy theme: a spiritual port of the Playdate game *Labyrinth* with its own dungeon generator (bigger rooms and a longer view), color and sound.

Highlights
- A roguelike dungeon crawl: secret doors, fountains, chests, bosses, an elevator down, turn-based combat, and a town with a Temple, a Blacksmith and the BeastMart.
- Permadeath, with a local Legends leaderboard. A dead hero haunts the floor where he fell.
- Follows your Omarchy theme live; an optional CRT glow; the original's sound effects and music, with a mute switch.
- Made to stay open: an idle window uses about 0% CPU.
- Two ways to draw the dungeon: text characters (default) or Pixel Art (Options, Dungeon style).

Omarchy plugin
- A bar widget that drops the game down under the bar, hides it with a click or a click anywhere outside it, and silences it while hidden.
- A LabyTerm entry in the launcher (SUPER+SPACE). The first launch builds the game from source (a few minutes).
- Install: omarchy plugin add https://github.com/SCharchian/omarchy-labyterm.git --enable

Saves are shared: a run started from the plugin (bar widget) can be saved and resumed from the launcher, or the other way around, because both use the same save file. There is one save slot, so if you run two copies at once, saving in one overwrites the other's save.

Requirements: Linux with a graphical session and SDL3. Developed and tested on Omarchy (Arch, Hyprland, Wayland); see KNOWN-ISSUES.md for other setups. Code is MIT; art, audio and game data are CC BY 4.0.
