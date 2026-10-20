#pragma once
#include <cstdint>
#include <vector>

#include "render/CellGrid.hpp"
#include "render/GlyphAtlas.hpp"

namespace laby {

// A frame of pixels, 0xAARRGGBB with A = 255.
struct Frame {
  int w = 0, h = 0;
  std::vector<std::uint32_t> px;
};

// Draws the grid with the atlas, one tile per cell: pixel = bg + (fg - bg) * coverage. Crisp, no effects.
// `withTorch` false leaves the torch's darkness out: draw it later with renderTorch, which only reads the grid's torch settings,
// so a picture that did not change is not rendered again for every step of the torch's breathing.
void renderGrid(const CellGrid& grid, const Atlas& atlas, Frame& out, bool withTorch = true);
void renderTorch(const CellGrid& grid, const Atlas& atlas, Frame& out);

// The old-terminal look, applied to a finished frame: horizontal beam spread, phosphor glow (bloom),
// dark gaps between raster lines, and a gentle vignette. All sizes scale with the cell height so the
// look is the same at every window size.
struct CrtParams {
  float softness = 0.45f;   // horizontal beam spread, 0..1
  float bloom = 0.45f;      // glow strength on the UI (text, frames, panels)
  float viewportBloom = 0.15f;  // glow strength inside the viewport rectangle (the picture); glow from the UI still spills in a little
  float scanlines = 0.30f;  // darkness of the gaps between raster lines, 0..1
  float vignette = 0.10f;   // corner darkening
  float viewportThreshold = 0.78f;  // brightness (0..1) above which things in the viewport glow
  float glowFloor = 0;      // nothing at or below this brightness (0..1) is a glow source: the screen's background level, so a light theme's paper does not bloom
  float monsterBloom = 0;   // the same for the monster in a fight (images marked ImageRun::glow)
  float objectBloom = 0;    // glow strength on 3D objects (cells marked Cell::glow), whatever their brightness; set by effectiveCrtParams
  // 0 = off (crisp), 1 = subtle, 2 = default, 3 = heavy
  static CrtParams level(int n);
};
// `viewport` (pixels, end exclusive; all zero = none) is where `viewportBloom` applies instead of `bloom`.
struct PixelRect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
// The CRT level's own strengths scaled by the user's live-tunable `display` knobs (tuning.json).
CrtParams effectiveCrtParams(int level);
// `p` for a screen whose background is `bg`: what is no brighter than the background (plus a little) never glows. A dark background changes
// nothing (its level is under the UI's threshold); a light theme would otherwise bloom over the whole screen and wash the text out.
CrtParams withBackground(CrtParams p, Rgb bg);
// Which cells hold 3D object ink (Cell::glow) or a monster (ImageRun::glow), for the glow pass: per cell a color (r, g, b) and an alpha:
// 255 = object cell, 191 = object cell drawn as a text character (glow x kTextGlowBoost), 127 = monster cell, 0 = none. The color is the cell's own background (a monster's body color for its cells). Only the
// pixels brighter than that color glow, so the thing's lines light up and the paper around them does not.
struct GlowMask {
  int cols = 0, rows = 0, cellW = 0, cellH = 0;
  std::vector<std::uint8_t> rgba;  // cols * rows * 4
  bool any = false;                // at least one glowing cell
  static constexpr std::uint8_t kObject = 255, kObjectText = 191, kMonster = 127;
};
GlowMask makeGlowMask(const CellGrid& grid, const Atlas& atlas);
// How far above the cell's background a pixel must be to glow (0..1): keeps rounding and beam-spread leakage out of it.
constexpr float kObjectGlowFloor = 0.04f;
// A character inks about half of its cell, where an octant block can ink all of it, so object cells drawn as text glow this much more (the GPU
// shader, crt_bright.frag, uses the same number: keep them equal).
constexpr float kTextGlowBoost = 3.0f;

void applyCrt(Frame& frame, int cellHeight, const CrtParams& p, const PixelRect& viewport = {}, const GlowMask* glow = nullptr);

// The grid's viewport rectangle in frame pixels (zero if the grid has none).
PixelRect viewportPixels(const CellGrid& grid, const Atlas& atlas);

}  // namespace laby
