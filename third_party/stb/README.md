# stb (vendored)

Single-header libraries from https://github.com/nothings/stb (commit f1c79c0), public domain / MIT:

- `stb_image.h`: PNG loading (used to turn the original Labyrinth art into glyph mosaics)
- `stb_truetype.h`: TrueType rasterizing (used to build the text part of the glyph atlas)

Implementations are compiled once in `src/render/StbImpl.cpp`.
