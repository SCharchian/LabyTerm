#include "ui/ShaftArt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace laby {
namespace {

double hash01(std::int64_t a, std::int64_t b = 0, std::int64_t c = 0) {
  std::uint64_t h = static_cast<std::uint64_t>(a) * 0x9E3779B97F4A7C15ull ^ static_cast<std::uint64_t>(b) * 0xC2B2AE3D27D4EB4Full ^ static_cast<std::uint64_t>(c) * 0x165667B19E3779F9ull;
  h ^= h >> 32;
  h *= 0xD6E8FEB86659FD93ull;
  h ^= h >> 32;
  return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

constexpr int kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
double bayer(int x, int y) { return (kBayer4[y & 3][x & 3] + 0.5) / 16.0; }

// Shapes are measured in picture units (one 2x4-per-cell sub-pixel); a signed distance <= 0 is inside.
double boxDist(double x, double y, double cx, double cy, double hx, double hy) {
  const double dx = std::fabs(x - cx) - hx, dy = std::fabs(y - cy) - hy;
  return std::hypot(std::max(dx, 0.0), std::max(dy, 0.0)) + std::min(std::max(dx, dy), 0.0);
}
double segDist(double x, double y, double ax, double ay, double bx, double by, double r) {
  const double vx = bx - ax, vy = by - ay, t = std::clamp(((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy), 0.0, 1.0);
  return std::hypot(x - (ax + t * vx), y - (ay + t * vy)) - r;
}

constexpr double kBaseW = 186;  // the game's viewport in units; a wider picture (a big window) gets the same rails, kept the same distance from its edges
constexpr double kRailInset = 26, kRailHalf = 3;

// Position of the i-th thing in a column of roughly `pitch` units, jittered so the spacing is never regular (a regular
// pattern would also look like it runs backwards at the wrong speed).
double slot(int i, double pitch, double jitter, int salt) { return i * pitch + (hash01(i, salt) - 0.5) * 2 * jitter; }

}  // namespace

void paintShaft(ViewLayers& L, double scroll) {
  const int d = L.density > 0 ? L.density : std::max(1, L.walls.w / static_cast<int>(kBaseW));
  const int W = L.walls.w, H = L.walls.h;
  const double kW = std::max(kBaseW, static_cast<double>(W) / d);  // the picture's real width in units: the cart sits in its middle, so the shaft must too
  const double kRailL = kRailInset, kRailR = kW - kRailInset;
  const double mid = kW / 2;
  const int backShift = static_cast<int>(std::lround(scroll * 0.55 * d));  // the back wall is the farthest: slowest
  constexpr double kBeamPitch = 58, kClampPitch = 41, kLampPitch = 170;
  constexpr int kStreaks = 20;

  for (int py = 0; py < H; ++py) {
    for (int px = 0; px < W; ++px) {
      const double x = (px + 0.5) / d, y = (py + 0.5) / d;
      const double wy = y - scroll;  // the scenery's own coordinate: a feature fixed in it moves down the picture as `scroll` grows
      auto set = [&](Bitmap1& b, std::uint8_t v) { b.px[static_cast<size_t>(py) * static_cast<size_t>(W) + static_cast<size_t>(px)] = v; };

      // --- near structure (walls layer): rails, clamps, timber cross-sets, lantern brackets
      double sd = 1e9;
      sd = std::min(sd, boxDist(x, 0, kRailL, 0, kRailHalf, 1e9));
      sd = std::min(sd, boxDist(x, 0, kRailR, 0, kRailHalf, 1e9));
      const int c0 = static_cast<int>(std::floor(wy / kClampPitch));
      for (int i = c0 - 1; i <= c0 + 1; ++i) {  // clamp plates on the rails
        const double yc = slot(i, kClampPitch, 7, 1);
        sd = std::min(sd, boxDist(x, wy, kRailL, yc, 6.5, 3.5));
        sd = std::min(sd, boxDist(x, wy, kRailR, yc + 17, 6.5, 3.5));
      }
      const int b0 = static_cast<int>(std::floor(wy / kBeamPitch));
      for (int i = b0 - 1; i <= b0 + 1; ++i) {  // cross-sets: a beam rail to rail, braced at the corners now and then
        const double yc = slot(i, kBeamPitch, 9, 2);
        sd = std::min(sd, boxDist(x, wy, mid, yc, mid - kRailL - kRailHalf, 3.2));
        if (hash01(i, 3) > 0.35) {
          sd = std::min(sd, segDist(x, wy, kRailL + kRailHalf, yc + 3, kRailL + kRailHalf + 24, yc + 27, 1.5));
          sd = std::min(sd, segDist(x, wy, kRailR - kRailHalf, yc + 3, kRailR - kRailHalf - 24, yc + 27, 1.5));
        }
        if (hash01(i, 4) > 0.7) sd = std::min(sd, boxDist(x, wy, mid, yc + 14, 2.2, 11));  // a king post under the beam
      }
      const int l0 = static_cast<int>(std::floor(wy / kLampPitch));
      double glow = 0, glowX = 0, glowY = 0;
      bool window = false;
      for (int i = l0 - 1; i <= l0 + 1; ++i) {  // lanterns hang from an arm on one rail or the other
        const bool left = hash01(i, 5) < 0.5;
        const double yl = slot(i, kLampPitch, 40, 6), sx = left ? kRailL + kRailHalf : kRailR - kRailHalf, dir = left ? 1 : -1;
        const double lx = sx + dir * 13;
        sd = std::min(sd, segDist(x, wy, sx, yl, lx, yl, 1.0));            // the arm
        sd = std::min(sd, segDist(x, wy, lx, yl, lx, yl + 6, 0.5));        // the chain
        const double body = boxDist(x, wy, lx, yl + 12, 4.2, 5.5);
        sd = std::min(sd, body);
        if (body < -1.6) window = true;                                    // the lit pane: ink-free, so the lantern reads as bright against its outline
        const double gd = std::hypot(x - lx, wy - (yl + 12));
        if (gd < 26 && 1 - gd / 26 > glow) { glow = 1 - gd / 26; glowX = lx; glowY = yl + 12; }
      }
      (void)glowX; (void)glowY;
      if (sd <= 0) set(L.walls, window ? Bitmap1::Dark : Bitmap1::Light);
      else if (sd <= 1.0) set(L.walls, Bitmap1::Dark);  // the clear outline that parts a lit bar from the wall behind it

      // --- mid layer (shade): lantern glow, dust streaks
      if (glow > 0 && bayer(px, py) < glow * glow * 0.9) set(L.shade, Bitmap1::Light);
      for (int k = 0; k < kStreaks; ++k) {
        const double sx = 3 + hash01(k, 7) * (kW - 6), len = 5 + hash01(k, 8) * 6, rate = 1.6 + hash01(k, 9) * 0.8;
        const double span = H / static_cast<double>(d) + 24;
        double sy = std::fmod(hash01(k, 10) * span + scroll * rate, span);
        if (sy < 0) sy += span;
        sy -= 12;
        if (std::fabs(x - sx) < 0.55 && y > sy && y < sy + len) set(L.shade, Bitmap1::Light);
      }

      // --- back wall (background layer): rough-hewn rock. Cracks between irregular facets, hatching on some facets, and ore veins
      // (dense, glittering ink) that come and go as they wander down the wall. Ink is the cracks and veins, so the wall stays sparse.
      const int wpy = py - backShift;
      const double ys = static_cast<double>(wpy) / d;
      const double wx = x + 3.2 * std::sin(ys * 0.21 + x * 0.07), wyy = ys + 3.2 * std::sin(x * 0.19 + ys * 0.05);  // ragged: the facets' edges wobble
      constexpr double kFacet = 27;
      const int gi = static_cast<int>(std::floor(wx / kFacet)), gj = static_cast<int>(std::floor(wyy / kFacet));
      double d1 = 1e9, d2 = 1e9, ax = 0, ay = 0, bx = 0, by = 0;
      int ai = 0, aj = 0, bi = 0, bj = 0;
      for (int j = gj - 1; j <= gj + 1; ++j)
        for (int i = gi - 1; i <= gi + 1; ++i) {
          const double sx = (i + 0.1 + 0.8 * hash01(i, j, 21)) * kFacet, sy = (j + 0.1 + 0.8 * hash01(i, j, 22)) * kFacet;
          const double dd = std::hypot(wx - sx, wyy - sy);
          if (dd < d1) { d2 = d1; bx = ax; by = ay; bi = ai; bj = aj; d1 = dd; ax = sx; ay = sy; ai = i; aj = j; }
          else if (dd < d2) { d2 = dd; bx = sx; by = sy; bi = i; bj = j; }
        }
      const double sep = std::hypot(ax - bx, ay - by);
      const double edge = sep > 1e-9 ? (d2 * d2 - d1 * d1) / (2 * sep) : 1e9;  // distance to the crack between the two facets
      bool ink = false;
      if (hash01(ai + bi * 31, aj + bj * 17, 23) > 0.28 && edge < 0.55 + 0.7 * hash01(ai * 7 + bi, aj * 5 + bj, 24)) ink = true;  // some facets run into each other: the cracks do not close up
      if (!ink && hash01(ai, aj, 25) < 0.3 && std::fmod(std::fmod(wx + wyy, 6.0) + 6.0, 6.0) < 0.9 && d1 < kFacet * 0.42) ink = true;  // a few facets are hatched (tool marks)
      if (!ink && hash01(px, wpy, 26) < 0.014) ink = true;  // flecks
      for (int k = 0; k < 4 && !ink; ++k) {  // ore veins
        const double x0 = (k + 0.5) * kW / 4 + (hash01(k, 27) - 0.5) * 22;
        const double centre = x0 + 13 * std::sin(ys / 37 + k * 2.1) + 5 * std::sin(ys / 11 + k);
        const double reach = 1.0 + 2.2 * std::sin(ys / 53 + k * 1.7);  // thicker, then pinching out
        const double off = std::fabs(x - centre);
        if (reach > 0.4 && off < reach && bayer(px, wpy) < 0.8) ink = true;
        else if (reach > 0.4 && off < reach + 2.6 && hash01(px, wpy, 28) < 0.09) ink = true;  // glints round the vein
      }
      if (ink) set(L.background, Bitmap1::Light);
    }
  }
}

void shadeBehindCart(ViewLayers& L, const ViewLayers& cart) {
  const int W = L.walls.w, H = L.walls.h;
  std::vector<const Bitmap1*> layers{&cart.objects, &cart.shade};
  for (const auto& t : cart.tints) {
    layers.push_back(&t.lit);
    layers.push_back(&t.shade);
  }
  for (int y = 0; y < H; ++y) {
    int lo = W, hi = -1;
    for (const Bitmap1* b : layers) {
      if (b->w != W) continue;
      for (int x = 0; x < W; ++x)
        if (b->px[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)] != Bitmap1::Clear) {
          lo = std::min(lo, x);
          break;
        }
      for (int x = W - 1; x >= 0; --x)
        if (b->px[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)] != Bitmap1::Clear) {
          hi = std::max(hi, x);
          break;
        }
    }
    for (int x = lo; x <= hi; ++x) {
      std::uint8_t& w = L.walls.px[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)];
      if (w == Bitmap1::Light) {
        w = Bitmap1::Clear;
        L.shade.px[static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)] = Bitmap1::Light;
      }
    }
  }
}

CartShake cartShake(double ms) {
  // Think of it in slow motion: each jolt eases in over ~120 ms (3+ frames at 33-40 ms), swings out and back in a slow, soft bounce
  // (a ~400 ms swing) and dies away over about a second, so no frame is far from its neighbours. The cart is thrown sideways further
  // than it drops.
  constexpr double kPi = 3.14159265358979, kPeriod = 450, kAttack = 140;
  double y = 0.9 * std::sin(ms / 1150 * 2 * kPi), x = 1.2 * std::sin(ms / 1700 * 2 * kPi + 1);  // the slow sway
  const int k0 = static_cast<int>(std::floor(ms / kPeriod));
  for (int k = k0 - 4; k <= k0; ++k) {
    const double t = ms - (k * kPeriod + (hash01(k, 31) - 0.5) * 200);  // time since this jolt
    if (t < 0 || t > 1400) continue;
    const double up = std::clamp(t / kAttack, 0.0, 1.0), ease = up * up * (3 - 2 * up);  // smoothstep: no jump at the start
    const double env = ease * std::exp(-t / 300);
    const double dropAmp = 3.0 + 2.0 * hash01(k, 32), sideAmp = 4.5 + 3.0 * hash01(k, 33);  // px
    y += (hash01(k, 34) < 0.8 ? 1 : -1) * dropAmp * env * std::sin(2 * kPi * t / 560);
    x += (hash01(k, 35) < 0.5 ? -1 : 1) * sideAmp * env * std::sin(2 * kPi * t / 760 + 0.5 * hash01(k, 36));
  }
  return {static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))};
}

}  // namespace laby
