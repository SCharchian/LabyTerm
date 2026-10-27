#include "ui/BossAura.hpp"

#include <tuple>
#include <utility>
#include <vector>

namespace laby {

// A Boss10's glow: two rings round the picture's silhouette (a solid one, then a dithered one), as images the size of the picture plus a margin.
// Built once per picture. Pixels are 2x-art pixels, like the picture's own.
Aura auraFor(const std::shared_ptr<const Image8>& img, bool dither) {
  static std::vector<std::tuple<const Image8*, bool, Aura>> cache;
  for (const auto& e : cache)
    if (std::get<0>(e) == img.get() && std::get<1>(e) == dither) return std::get<2>(e);
  constexpr int kPad = 22, kInner = 9, kOuter = 21;
  const int W = img->w + 2 * kPad, H = img->h + 2 * kPad;
  // chamfer distance (3-4) from the silhouette
  std::vector<int> d(static_cast<size_t>(W) * H, 1 << 20);
  for (int y = 0; y < img->h; ++y)
    for (int x = 0; x < img->w; ++x)
      if (img->alpha[static_cast<size_t>(y) * img->w + x] > 128) d[static_cast<size_t>(y + kPad) * W + x + kPad] = 0;
  const auto at = [&](int x, int y) -> int& { return d[static_cast<size_t>(y) * W + x]; };
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      int v = at(x, y);
      if (x > 0) v = std::min(v, at(x - 1, y) + 3);
      if (y > 0) v = std::min(v, at(x, y - 1) + 3);
      if (x > 0 && y > 0) v = std::min(v, at(x - 1, y - 1) + 4);
      if (x + 1 < W && y > 0) v = std::min(v, at(x + 1, y - 1) + 4);
      at(x, y) = v;
    }
  for (int y = H - 1; y >= 0; --y)
    for (int x = W - 1; x >= 0; --x) {
      int v = at(x, y);
      if (x + 1 < W) v = std::min(v, at(x + 1, y) + 3);
      if (y + 1 < H) v = std::min(v, at(x, y + 1) + 3);
      if (x + 1 < W && y + 1 < H) v = std::min(v, at(x + 1, y + 1) + 4);
      if (x > 0 && y + 1 < H) v = std::min(v, at(x - 1, y + 1) + 4);
      at(x, y) = v;
    }
  const auto make = [&](int lo, int hi, bool dither) {
    auto m = std::make_shared<Image8>();
    m->w = W;
    m->h = H;
    m->tone.assign(static_cast<size_t>(W) * H, 0);
    m->alpha.assign(static_cast<size_t>(W) * H, 0);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const int dist = at(x, y) / 3;
        if (dist > lo && dist <= hi && (!dither || (x / 2 + y / 2) % 2 == 0)) m->alpha[static_cast<size_t>(y) * W + x] = 255;
      }
    return m;
  };
  Aura a{make(0, kInner, false), make(kInner, kOuter, dither), kPad};
  cache.emplace_back(img.get(), dither, a);
  return a;
}

}  // namespace laby
