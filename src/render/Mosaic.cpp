#include <cstdlib>
#include <algorithm>
#include "render/Mosaic.hpp"

#include <algorithm>
#include <cstdio>

#include "render/GlyphAtlas.hpp"
#include "render/TextShapes.hpp"
#include "stb_image.h"

namespace laby {

Bitmap1 Bitmap1::loadPng(const std::string& path) {
  Bitmap1 b;
  int w = 0, h = 0, n = 0;
  unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (!px) return b;
  b = Bitmap1(w, h);
  for (int i = 0; i < w * h; ++i) {
    const unsigned char r = px[i * 4], g = px[i * 4 + 1], bl = px[i * 4 + 2], a = px[i * 4 + 3];
    b.px[static_cast<size_t>(i)] = a <= 127 ? Clear : ((r + g + bl) / 3 < 128 ? Dark : Light);
  }
  stbi_image_free(px);
  return b;
}

void Bitmap1::blit(const Bitmap1& src, int x0, int y0) {
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x) {
      const std::uint8_t v = src.at(x, y);
      const int tx = x0 + x, ty = y0 + y;
      if (v != Clear && tx >= 0 && ty >= 0 && tx < w && ty < h) px[static_cast<size_t>(ty) * static_cast<size_t>(w) + static_cast<size_t>(tx)] = v;
    }
}

void Bitmap1::eraseWhere(const Bitmap1& mask, int x0, int y0) {
  for (int y = 0; y < mask.h; ++y)
    for (int x = 0; x < mask.w; ++x) {
      const int tx = x0 + x, ty = y0 + y;
      if (mask.at(x, y) != Clear && tx >= 0 && ty >= 0 && tx < w && ty < h) px[static_cast<size_t>(ty) * static_cast<size_t>(w) + static_cast<size_t>(tx)] = Clear;
    }
}

Bitmap1 Bitmap1::cropped(int x0, int y0, int cw, int chh) const {
  Bitmap1 o(cw, chh);
  for (int y = 0; y < chh; ++y)
    for (int x = 0; x < cw; ++x) o.px[static_cast<size_t>(y) * static_cast<size_t>(cw) + static_cast<size_t>(x)] = at(x0 + x, y0 + y);
  return o;
}

Bitmap1 Bitmap1::inverted() const {
  Bitmap1 o = *this;
  for (auto& v : o.px) v = v == Dark ? Light : v == Light ? Dark : Clear;
  return o;
}

Bitmap1 Bitmap1::scaledTo(int nw, int nh) const {
  if (nw == w && nh == h) return *this;
  Bitmap1 o(nw, nh);
  for (int y = 0; y < nh; ++y)
    for (int x = 0; x < nw; ++x) {
      const int x0 = x * w / nw, x1 = std::max(x0 + 1, (x + 1) * w / nw);
      const int y0 = y * h / nh, y1 = std::max(y0 + 1, (y + 1) * h / nh);
      int dark = 0, light = 0, total = 0;
      for (int sy = y0; sy < y1; ++sy)
        for (int sx = x0; sx < x1; ++sx) {
          const std::uint8_t v = at(sx, sy);
          dark += v == Dark;
          light += v == Light;
          ++total;
        }
      std::uint8_t v = Clear;
      if (dark * 3 >= total) v = Dark;          // keep thin dark lines
      else if (light * 2 >= total) v = Light;
      o.px[static_cast<size_t>(y) * static_cast<size_t>(nw) + static_cast<size_t>(x)] = v;
    }
  return o;
}

const char* mosaicName(MosaicMode m) {
  switch (m) {
    case MosaicMode::Octant2x4: return "octants 2x4 (square sub-pixels)";
    case MosaicMode::Sextant2x3: return "sextants 2x3 (real Unicode)";
    case MosaicMode::Quadrant2x2: return "quadrants 2x2 (real Unicode)";
    case MosaicMode::Half1x2: return "half blocks 1x2";
    case MosaicMode::AsciiRamp: return "ASCII density ramp";
  }
  return "?";
}

MosaicMode nextMosaicMode(MosaicMode m) {
  return static_cast<MosaicMode>((static_cast<int>(m) + 1) % 5);
}

namespace {
struct Grid { int px, py; };  // pixels per cell
Grid gridFor(MosaicMode m) {
  switch (m) {
    case MosaicMode::Octant2x4: return {2, 4};
    case MosaicMode::Sextant2x3: return {2, 3};
    case MosaicMode::Quadrant2x2: return {2, 2};
    case MosaicMode::Half1x2: return {1, 2};
    case MosaicMode::AsciiRamp: return {2, 4};
  }
  return {2, 4};
}
}  // namespace

CellSize mosaicSize(const Bitmap1& b, MosaicMode mode) {
  const Grid g = gridFor(mode);
  return {(b.w + g.px - 1) / g.px, (b.h + g.py - 1) / g.py};
}

void drawMosaic(CellGrid& grid, int cx, int cy, const Bitmap1& b, MosaicMode mode, Rgb ink, Rgb paper, bool invert) {
  const std::uint8_t inkValue = invert ? Bitmap1::Light : Bitmap1::Dark;
  const Grid g = gridFor(mode);
  const CellSize sz = mosaicSize(b, mode);
  static const char32_t kRamp[] = U" .:-=+*#%@";
  for (int row = 0; row < sz.rows; ++row)
    for (int col = 0; col < sz.cols; ++col) {
      unsigned pattern = 0;
      int count = 0;
      for (int j = 0; j < g.py; ++j)
        for (int i = 0; i < g.px; ++i)
          if (b.at(col * g.px + i, row * g.py + j) == inkValue) {
            pattern |= 1u << (j * g.px + i);
            ++count;
          }
      char32_t ch = U' ';
      switch (mode) {
        case MosaicMode::Octant2x4: ch = pattern ? mosaicGlyph(pattern) : U' '; break;
        case MosaicMode::Sextant2x3: ch = sextantGlyph(pattern); break;
        case MosaicMode::Quadrant2x2: ch = quadrantGlyph(pattern); break;
        case MosaicMode::Half1x2: ch = pattern == 0 ? U' ' : pattern == 1 ? U'▀' : pattern == 2 ? U'▄' : U'█'; break;
        case MosaicMode::AsciiRamp: ch = kRamp[count * 9 / 8]; break;
      }
      grid.put(cx + col, cy + row, ch, ink, paper);
    }
}

void drawMosaicLayered(CellGrid& grid, int cx, int cy, const Bitmap1& base, const Bitmap1& over, Rgb baseInk, Rgb overInk, Rgb paper, bool invert) {
  const std::uint8_t inkValue = invert ? Bitmap1::Light : Bitmap1::Dark;
  const CellSize sz = mosaicSize(base, MosaicMode::Octant2x4);
  for (int row = 0; row < sz.rows; ++row)
    for (int col = 0; col < sz.cols; ++col) {
      unsigned pBase = 0, pOver = 0;
      bool solid = false;  // any opaque pixel of the overlay (ink or not) in this cell
      for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 2; ++i) {
          const int x = col * 2 + i, y = row * 4 + j;
          if (base.at(x, y) == inkValue) pBase |= 1u << (j * 2 + i);
          const std::uint8_t o = over.at(x, y);
          solid |= o != Bitmap1::Clear;
          if (o == inkValue) pOver |= 1u << (j * 2 + i);
        }
      // An opaque overlay pixel hides what is behind it, even where it is not ink: the cell then shows the
      // overlay alone (a non-ink opaque pixel is simply dark), so a monster's belly is solid, not see-through.
      const unsigned p = solid ? pOver : pBase;
      grid.put(cx + col, cy + row, p ? mosaicGlyph(p) : U' ', solid ? overInk : baseInk, paper);
    }
}

namespace {
int colourDistance(Rgb a, Rgb b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); }
}  // namespace

void drawMosaicStack(CellGrid& grid, int cx, int cy, const std::vector<MosaicLayer>& layers, Rgb paper, bool invert, int density) {
  if (layers.empty()) return;
  const std::uint8_t inkValue = invert ? Bitmap1::Light : Bitmap1::Dark;
  const int sw = 2 * density, sh = 4 * density, n = sw * sh;  // the samples across a cell: 2x4 (octants), or 4x8 (text, which needs the finer picture)
  const bool text = density > 1;
  const Bitmap1& first = *layers.front().bitmap;
  const int cols = (first.w + sw - 1) / sw, rows = (first.h + sh - 1) / sh;
  std::vector<Rgb> pc(static_cast<size_t>(n)), cand(static_cast<size_t>(n) + 1);
  std::vector<char> glowPx(static_cast<size_t>(n));
  std::vector<int> weight(static_cast<size_t>(n));
  for (int row = 0; row < rows; ++row)
    for (int col = 0; col < cols; ++col) {
      // Each of the cell's pixels comes from the top-most layer that is opaque at that pixel (so a lit pixel of the
      // floor next to a wall is kept, not erased because the wall owns the rest of the cell): the layer's ink where it is
      // lit, the paper where it is dark.
      std::fill(glowPx.begin(), glowPx.end(), 0);
      for (int j = 0; j < sh; ++j)
        for (int i = 0; i < sw; ++i) {
          Rgb c = paper;
          for (size_t li = 0; li < layers.size(); ++li) {
            const std::uint8_t v = layers[li].bitmap->at(col * sw + i, row * sh + j);
            if (v == Bitmap1::Clear) continue;  // see through: look at the layer below
            if (v == inkValue) {
              c = layers[li].ink;
              glowPx[static_cast<size_t>(j * sw + i)] = layers[li].glow;
            }
            break;
          }
          pc[static_cast<size_t>(j * sw + i)] = c;
        }
      // A cell can show only two colours (ink over paper). Where more than two are present (a bright strand over a shaded
      // floor over black), use the pair that fits the pixels best, and show every pixel in the nearer of the two.
      int nc = 0;
      cand[static_cast<size_t>(nc++)] = paper;
      for (const Rgb& c : pc) {
        bool seen = false;
        for (int k = 0; k < nc; ++k) seen |= cand[static_cast<size_t>(k)] == c;
        if (!seen) cand[static_cast<size_t>(nc++)] = c;
      }
      // A lit pixel matters more than an empty one (a thin bright bar or a shaded strip must not be lost, or painted over, to
      // keep a few black pixels black), in proportion to how far it is from the paper.
      int maxFromPaper = 1;
      for (int k = 1; k < nc; ++k) maxFromPaper = std::max(maxFromPaper, colourDistance(cand[static_cast<size_t>(k)], paper));
      for (int k = 0; k < n; ++k) {
        weight[static_cast<size_t>(k)] = 16 + 64 * colourDistance(pc[static_cast<size_t>(k)], paper) / maxFromPaper;
        // Text shows one ink colour per cell, so where an object's edge meets the wall its pixels decide the colour: the object keeps
        // its own colour and its glow, rather than a character in the wall's colour that glows.
        if (text && glowPx[static_cast<size_t>(k)]) weight[static_cast<size_t>(k)] *= 3;
      }
      Rgb bestP = paper, bestI = paper;
      if (nc > 1) {
        long bestCost = 1L << 60;
        for (int pi = 0; pi < nc; ++pi)
          for (int ii = 0; ii < nc; ++ii) {
            if (pi == ii || (text && pi != 0)) continue;  // text: the paper is always the background, so no character cell becomes a flat block
            long cost = 0;
            for (int k = 0; k < n; ++k)
              cost += static_cast<long>(weight[static_cast<size_t>(k)]) * std::min(colourDistance(pc[static_cast<size_t>(k)], cand[static_cast<size_t>(pi)]), colourDistance(pc[static_cast<size_t>(k)], cand[static_cast<size_t>(ii)]));
            if (cost < bestCost) {  // (ties keep the earlier pair: the paper as paper, then the top-most ink)
              bestCost = cost;
              bestP = cand[static_cast<size_t>(pi)];
              bestI = cand[static_cast<size_t>(ii)];
            }
          }
      }
      std::uint32_t pattern = 0;
      if (nc > 1)
        for (int k = 0; k < n; ++k) {
          const int dP = colourDistance(pc[static_cast<size_t>(k)], bestP), dI = colourDistance(pc[static_cast<size_t>(k)], bestI);
          if (dI < dP || (dI == dP && pc[static_cast<size_t>(k)] == bestI)) pattern |= 1u << k;
        }
      grid.put(cx + col, cy + row, pattern ? (text ? bestTextGlyph(pattern) : mosaicGlyph(pattern)) : U' ', bestI, bestP);
      for (int k = 0; k < n; ++k)
        if ((pattern >> k & 1u) && glowPx[static_cast<size_t>(k)] && pc[static_cast<size_t>(k)] == bestI) {  // (only ink in the cell's own colour: not a wall-coloured character beside an object)
          if (grid.inBounds(cx + col, cy + row)) grid.at(cx + col, cy + row).glow = text ? 2 : 1;
          break;
        }
    }
}

}  // namespace laby
