#include "render/TermFrame.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/Tuning.hpp"
#include "platform/Theme.hpp"
#include "render/GlyphAtlas.hpp"

namespace laby {
namespace {

int luma(Rgb c) { return c.r * 3 + c.g * 6 + c.b; }

// One sub-pixel of the picture layer: 2 per cell across, 4 down. `has` = a picture covers it.
struct Sub {
  Rgb col;
  bool has = false;
};

// Draws one picture into the sub-pixel layer. The picture's pixels are averaged over each sub-pixel they cover
// (the same placement as FrameRenderer's placeImage, with one sub-pixel as the unit); coverage under half is clear.
void rasterize(const ImageRun& im, int subW, int subH, std::vector<Sub>& layer) {
  const Image8& I = *im.img;
  const float step = im.scale / static_cast<float>(im.srcScale);  // sub-pixels per image pixel
  const float ox = static_cast<float>(im.cx * 2 + im.sx), oy = static_cast<float>(im.cy * 4 + im.sy);
  const int x0 = std::max({im.clipX0 * 2, 0, static_cast<int>(std::floor(ox))});
  const int x1 = std::min({im.clipX1 * 2, subW, static_cast<int>(std::ceil(ox + static_cast<float>(I.w) * step))});
  const int y0 = std::max({im.clipY0 * 4, 0, static_cast<int>(std::floor(oy))});
  const int y1 = std::min({im.clipY1 * 4, subH, static_cast<int>(std::ceil(oy + static_cast<float>(I.h) * step))});
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      // the image pixels this sub-pixel covers (at least one)
      const float ua = (static_cast<float>(x) - ox) / step, ub = (static_cast<float>(x + 1) - ox) / step;
      const float va = (static_cast<float>(y) - oy) / step, vb = (static_cast<float>(y + 1) - oy) / step;
      const int u0 = std::clamp(static_cast<int>(std::floor(ua)), 0, I.w - 1), u1 = std::clamp(std::max(u0 + 1, static_cast<int>(std::ceil(ub))), 1, I.w);
      const int v0 = std::clamp(static_cast<int>(std::floor(va)), 0, I.h - 1), v1 = std::clamp(std::max(v0 + 1, static_cast<int>(std::ceil(vb))), 1, I.h);
      if (ub <= 0 || vb <= 0 || ua >= static_cast<float>(I.w) || va >= static_cast<float>(I.h)) continue;
      float a = 0, t = 0;
      for (int v = v0; v < v1; ++v)
        for (int u = u0; u < u1; ++u) {
          const size_t i = static_cast<size_t>(v) * static_cast<size_t>(I.w) + static_cast<size_t>(u);
          a += static_cast<float>(I.alpha[i]);
          t += static_cast<float>(I.alpha[i]) * static_cast<float>(I.tone[i]);
        }
      const float n = static_cast<float>((u1 - u0) * (v1 - v0));
      if (a / n < 127.5f) continue;
      const float tone = t / a / 255.0f;  // alpha-weighted, so a transparent neighbour does not darken the edge
      Sub& s = layer[static_cast<size_t>(y) * static_cast<size_t>(subW) + static_cast<size_t>(x)];
      s.has = true;
      s.col = Rgb{static_cast<std::uint8_t>(im.dark.r + (im.light.r - im.dark.r) * tone + 0.5f),
                  static_cast<std::uint8_t>(im.dark.g + (im.light.g - im.dark.g) * tone + 0.5f),
                  static_cast<std::uint8_t>(im.dark.b + (im.light.b - im.dark.b) * tone + 0.5f)};
    }
}

// Reduces the 8 sub-pixels of a cell to a glyph and two colors (the cell can show only two): the picture's
// colors where it covers, the cell's own background elsewhere; split into two groups by 2-means on brightness.
void reduceCell(Cell& cell, const std::array<Rgb, 8>& px) {
  int lo = 0, hi = 0;
  for (int i = 1; i < 8; ++i) {
    if (luma(px[static_cast<size_t>(i)]) < luma(px[static_cast<size_t>(lo)])) lo = i;
    if (luma(px[static_cast<size_t>(i)]) > luma(px[static_cast<size_t>(hi)])) hi = i;
  }
  Rgb a = px[static_cast<size_t>(lo)], b = px[static_cast<size_t>(hi)];  // dark and light centroids
  unsigned pattern = 0;
  for (int round = 0; round < 3; ++round) {
    int sa[3] = {0, 0, 0}, sb[3] = {0, 0, 0}, na = 0, nb = 0;
    pattern = 0;
    for (int i = 0; i < 8; ++i) {
      const Rgb c = px[static_cast<size_t>(i)];
      const int da = (c.r - a.r) * (c.r - a.r) + (c.g - a.g) * (c.g - a.g) + (c.b - a.b) * (c.b - a.b);
      const int db = (c.r - b.r) * (c.r - b.r) + (c.g - b.g) * (c.g - b.g) + (c.b - b.b) * (c.b - b.b);
      if (db < da) {
        pattern |= 1u << i;
        sb[0] += c.r; sb[1] += c.g; sb[2] += c.b; ++nb;
      } else {
        sa[0] += c.r; sa[1] += c.g; sa[2] += c.b; ++na;
      }
    }
    if (na) a = Rgb{static_cast<std::uint8_t>(sa[0] / na), static_cast<std::uint8_t>(sa[1] / na), static_cast<std::uint8_t>(sa[2] / na)};
    if (nb) b = Rgb{static_cast<std::uint8_t>(sb[0] / nb), static_cast<std::uint8_t>(sb[1] / nb), static_cast<std::uint8_t>(sb[2] / nb)};
  }
  cell.bg = a;
  cell.fg = b;
  cell.ch = pattern ? mosaicGlyph(pattern) : U' ';
  cell.glow = 0;
}

// The torch's light: the picture darkens towards its edges, as FrameRenderer's torch pass does, but per cell (a cell has two colours, so the
// light's fine dither is left out and more, smoother steps are used instead). Cells are measured in sub-pixels so the pool of light keeps its
// shape. Cells in `holes` (panels over the picture) are left alone.
void applyTorch(CellGrid& f, const CellGrid::Torch& tc) {
  const auto& k = tuning().torch;
  if (!tc.on || !k.enabled) return;
  const int x0 = std::max(0, tc.rect.x0), y0 = std::max(0, tc.rect.y0), x1 = std::min(f.width(), tc.rect.x1), y1 = std::min(f.height(), tc.rect.y1);
  if (x1 <= x0 || y1 <= y0) return;
  const double hw = (tc.rect.x1 - tc.rect.x0), hh = (tc.rect.y1 - tc.rect.y0) * 2.0;  // half the picture, in sub-pixels (2 wide, 4 tall per cell)
  const double mx = tc.rect.x0 * 2 + hw, my = tc.rect.y0 * 4 + hh;
  const double p = std::max(1.0, k.boxiness), invP = 1.0 / p, span = std::max(0.01, k.softness);
  const double maxDark = std::clamp(k.darkness + static_cast<double>(tc.pulse), 0.0, 1.0);
  const double r0 = k.radius + static_cast<double>(tc.breath), cxs = static_cast<double>(tc.shift) * hw;
  constexpr double kBands = 8;  // the SDL screen has 5 steps, dithered; a cell cannot dither, so it takes a few more (each step that changes recolours a ring of cells: 8 keeps the idle output near 50 KB/s at 185x40)
  std::vector<char> hole(static_cast<size_t>(f.width()) * static_cast<size_t>(f.height()), 0);
  for (const auto& r : tc.holes)
    for (int y = std::max(0, r.y0); y < std::min(f.height(), r.y1); ++y)
      for (int x = std::max(0, r.x0); x < std::min(f.width(), r.x1); ++x) hole[static_cast<size_t>(y) * static_cast<size_t>(f.width()) + static_cast<size_t>(x)] = 1;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      if (hole[static_cast<size_t>(y) * static_cast<size_t>(f.width()) + static_cast<size_t>(x)]) continue;
      const double dx = std::abs((x * 2 + 1.0 - mx - cxs) / hw), dy = std::abs((y * 4 + 2.0 - my) / hh);
      double t = std::clamp((std::pow(std::pow(dx, p) + std::pow(dy, p), invP) - r0) / span, 0.0, 1.0);
      if (t <= 0) continue;
      t = t * t * (3 - 2 * t);
      const double a = std::round(t * kBands) / kBands * maxDark;
      if (a <= 0) continue;
      Cell& c = f.at(x, y);
      c.fg = mix(c.fg, tc.dark, a);
      c.bg = mix(c.bg, tc.dark, a);
    }
}

}  // namespace

CellGrid terminalFrame(const CellGrid& g) {
  CellGrid f = g;
  const int subW = g.width() * 2, subH = g.height() * 4;

  if (!g.imageRuns().empty()) {
    std::vector<Sub> layer(static_cast<size_t>(subW) * static_cast<size_t>(subH));
    for (const ImageRun& im : g.imageRuns())
      if (im.img && !im.img->empty() && im.srcScale > 0 && im.scale > 0) rasterize(im, subW, subH, layer);
    for (int cy = 0; cy < g.height(); ++cy)
      for (int cx = 0; cx < g.width(); ++cx) {
        Cell& cell = f.at(cx, cy);
        std::array<Rgb, 8> px;
        bool any = false;
        for (int j = 0; j < 4; ++j)
          for (int i = 0; i < 2; ++i) {
            const Sub& s = layer[static_cast<size_t>(cy * 4 + j) * static_cast<size_t>(subW) + static_cast<size_t>(cx * 2 + i)];
            px[static_cast<size_t>(j * 2 + i)] = s.has ? s.col : cell.bg;
            any |= s.has;
          }
        if (any) reduceCell(cell, px);  // cells no picture touches keep their art
      }
  }

  // overlay text: one character per cell (the terminal has a single cell size)
  for (const TextRun& r : g.textRuns()) {
    const int x = r.cx + r.col, y = r.cy + r.row + static_cast<int>(std::lround(r.dy));
    if (!f.inBounds(x, y)) continue;
    Cell& c = f.at(x, y);
    c.ch = r.ch;
    c.fg = r.fg;
    c.glow = 0;
  }
  applyTorch(f, g.torch());
  f.clearOverlays();
  return f;
}

}  // namespace laby
