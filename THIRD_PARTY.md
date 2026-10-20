# Credits and third-party material

LabyTerm's own code is MIT (`LICENSE`); its art, audio and game data are CC BY 4.0 (`LICENSE-ART.txt`). See `LICENSING.md` for how to credit.
This page lists what else ships with the game or is used to build it.

## Libraries and fonts

| Piece | Version | License | Where used |
|---|---|---|---|
| stb_image.h, stb_image_write.h, stb_truetype.h (Sean Barrett) | commit f1c79c0, vendored in `third_party/stb/` | MIT or public domain (text in `third_party/stb/LICENSE`) | PNG loading and writing, TrueType rasterizing (`src/render/StbImpl.cpp`) |
| nlohmann/json | 3.11 or newer from the system, else v3.12.0 fetched at configure time | MIT | JSON data, tuning, settings, saves |
| SDL3 | system package, linked dynamically | zlib | window, input, audio output, GPU |
| Vulkan loader and driver | system, reached only through SDL3 GPU | system licenses | optional GPU screen filter |
| glslc (shaderc) | build tool only, optional; nothing of it ships | Apache-2.0 | compiles `src/render/shaders/*` to SPIR-V |
| VT323-Regular.ttf (Peter Hull and the VT323 Project Authors) | unmodified, `data/fonts/` | SIL OFL 1.1 (`data/fonts/OFL-VT323.txt`) | alternative terminal font |
| AdwaitaMono-Regular.ttf (Inter Project Authors, Renzhi Li, Jamie Gravendeel, Florian Müllner) | 51.0, unmodified, `data/fonts/` | SIL OFL 1.1 (`data/fonts/OFL-AdwaitaMono.txt`) | default text font |

For packagers: the OFL requires its license text to travel with the fonts, so ship `data/fonts/OFL-*.txt`, along with `third_party/stb/LICENSE`.
The nlohmann/json MIT notice should travel with a distributed binary (it is header-only and compiled in). SDL3's zlib notice is not required
for dynamic linking, but including it is courteous.

## Labyrinth, the Playdate game

LabyTerm's rules, data tables, pictures, sounds and text come from *Labyrinth* for the Playdate, written by Sam Charchian with additional
design by Paul Charchian, and are converted or reused here by the same author.

- **Art** (`data/art`): the monster sprites (up-scaled offline), the boss lair and its entrance frames, and the town, title and elevator
  pictures. All of it is the author's own. Some of the Playdate game's art was first generated with RetroDiffusion and then hand-finished
  by the author, as the in-game About page says; the author holds the rights and releases it under CC BY 4.0.
- **Audio** (`data/audio`): converted from the Playdate game's files. Its credits name [Freesound](https://freesound.org), with everything licensed CC0.
- **The dungeon generator** is new for LabyTerm. It is not derived from the Playdate game's generator or from any third-party code.

## Inspiration

The 1980 PLATO game *Labyrinth* by Randy Harmelink and Michael Wei is the inspiration for the whole line. No code or assets were taken from it.

## Not included

No part of the Playdate SDK, its fonts or its assets is included in this repository.
