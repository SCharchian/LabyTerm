#pragma once
#include <array>
#include <cstdint>

#include "render/CellGrid.hpp"
#include "render/GlyphAtlas.hpp"

namespace laby {

// Paints procedural art into the cells x0..x0+cols-1, y0..y0+rows-1: `sample(px, py)` gives the color of sub-pixel (px, py)
// (2x4 per cell). Each cell shows two colors: the sub-pixels brighter than the cell's mean in one, the rest in the other.
template <class Sample>
void paintMosaic(CellGrid& g, int x0, int y0, int cols, int rows, Sample&& sample) {
  const auto lum = [](Rgb c) { return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b; };
  for (int row = 0; row < rows; ++row)
    for (int col = 0; col < cols; ++col) {
      std::array<Rgb, 8> c{};
      double mean = 0;
      for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 2; ++i) {
          c[static_cast<size_t>(j * 2 + i)] = sample(col * 2 + i, row * 4 + j);
          mean += lum(c[static_cast<size_t>(j * 2 + i)]);
        }
      mean /= 8;
      unsigned pattern = 0;
      int nHi = 0, nLo = 0, rh = 0, gh = 0, bh = 0, rl = 0, gl = 0, bl = 0;
      for (int k = 0; k < 8; ++k) {
        const Rgb p = c[static_cast<size_t>(k)];
        if (lum(p) > mean + 1) {
          pattern |= 1u << k;
          ++nHi, rh += p.r, gh += p.g, bh += p.b;
        } else {
          ++nLo, rl += p.r, gl += p.g, bl += p.b;
        }
      }
      const auto avg = [](int s, int n) { return static_cast<std::uint8_t>((s + n / 2) / n); };
      const Rgb lo = nLo ? Rgb{avg(rl, nLo), avg(gl, nLo), avg(bl, nLo)} : Rgb{avg(rh, nHi), avg(gh, nHi), avg(bh, nHi)};
      const Rgb hi = nHi ? Rgb{avg(rh, nHi), avg(gh, nHi), avg(bh, nHi)} : lo;
      g.put(x0 + col, y0 + row, pattern ? mosaicGlyph(pattern) : U' ', hi, lo);
    }
}

}  // namespace laby
