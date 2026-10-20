# Changelog

All notable changes to LabyTerm. Versions follow [Semantic Versioning](https://semver.org/):
saves and settings stay compatible within a major version.

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
