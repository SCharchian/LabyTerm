#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "render/CellGrid.hpp"

namespace laby {

// A 1-bit picture with transparency. Each pixel is clear (not drawn), dark, or light, as the original
// Playdate art had it; "ink" is chosen when drawing, so inverting needs no reload.
struct Bitmap1 {
  enum : std::uint8_t { Clear = 0, Dark = 1, Light = 2 };
  int w = 0, h = 0;
  std::vector<std::uint8_t> px;

  Bitmap1() = default;
  Bitmap1(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * static_cast<size_t>(h_), Clear) {}
  std::uint8_t at(int x, int y) const { return (x >= 0 && y >= 0 && x < w && y < h) ? px[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] : static_cast<std::uint8_t>(Clear); }
  bool empty() const { return w == 0; }
  static Bitmap1 loadPng(const std::string& path);
  // Draw `src` at (x, y): its dark and light pixels overwrite this bitmap; its clear pixels leave it alone.
  void blit(const Bitmap1& src, int x, int y);
  // The w x h window at (x0, y0); anywhere outside this bitmap is clear. Whole-pixel, so nothing is resampled.
  Bitmap1 cropped(int x0, int y0, int w, int h) const;
  // Make every pixel of this bitmap clear where `mask` (placed at x, y) has an opaque pixel: a later sprite drawn over it.
  void eraseWhere(const Bitmap1& mask, int x, int y);
  // Dark <-> light swapped (clear stays clear).
  Bitmap1 inverted() const;
  // Resampled to w x h: each output pixel looks at the source pixels it covers. Dark wins when at least a
  // third of the covered area is dark, so thin dark lines (mortar, edges) survive shrinking.
  Bitmap1 scaledTo(int w, int h) const;
};

// Ways to turn pixels into text cells. Cells are 1:2, so 2x4 gives square sub-pixels.
enum class MosaicMode { Octant2x4, Sextant2x3, Quadrant2x2, Half1x2, AsciiRamp };
const char* mosaicName(MosaicMode m);
MosaicMode nextMosaicMode(MosaicMode m);

struct CellSize { int cols, rows; };
CellSize mosaicSize(const Bitmap1& bmp, MosaicMode mode);

// Draws bmp with its top-left cell at (cx, cy). Pixels that are "ink" use `ink`, everything else `paper`.
// invert = false: dark pixels are ink (lines drawn bright on a dark screen, like the Playdate poster).
// invert = true: light pixels are ink (solid blocks bright, mortar/cracks dark).
void drawMosaic(CellGrid& grid, int cx, int cy, const Bitmap1& bmp, MosaicMode mode, Rgb ink, Rgb paper, bool invert);

// Two layers drawn together: `over` (monsters, objects) wins in any cell where it has an opaque pixel (dark
// or light, not clear) and is drawn with `overInk`; everywhere else `base` (walls, doors) is drawn with
// `baseInk`. A cell can show only one ink color, so wall pixels in a cell the monster touches are dropped,
// which also gives the monster a clean halo. Octant mosaics only. Both bitmaps must be the same size.
void drawMosaicLayered(CellGrid& grid, int cx, int cy, const Bitmap1& base, const Bitmap1& over, Rgb baseInk, Rgb overInk, Rgb paper, bool invert);

// One layer of a mosaic stack: a bitmap and the color of its ink.
struct MosaicLayer {
  const Bitmap1* bitmap;
  Rgb ink;
  bool glow = false;  // cells showing this layer's ink are marked `Cell::glow` (objects: the CRT filter makes them glow)
};

// Draws a stack of same-sized layers, listed top first. In each cell the topmost layer that has an opaque pixel (dark or
// light, not clear) there wins and the cell shows only that layer's ink pixels, in its color, over `paper`. So a wall
// hides the floor behind it and an object or monster hides the wall, and a figure never shows wall pixels through
// its body.
// density 1: octant mosaics, the layers hold 2x4 pixels per cell. density 2: text, the layers hold 4x8 pixels per cell and each
// cell shows the printable character whose shape fits its pixels best (render/TextShapes).
void drawMosaicStack(CellGrid& grid, int cx, int cy, const std::vector<MosaicLayer>& layers, Rgb paper, bool invert, int density = 1);

}  // namespace laby
