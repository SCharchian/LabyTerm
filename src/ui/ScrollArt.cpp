#include "ui/ScrollArt.hpp"

#include <algorithm>
#include <cmath>

#include "platform/Theme.hpp"
#include "ui/MosaicPaint.hpp"

namespace laby {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr Rgb kWhite{255, 255, 255};

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double smooth(double a, double b, double v) {
  const double u = clamp01((v - a) / (b - a));
  return u * u * (3 - 2 * u);
}
double hash(int a, int b) {  // a stable pseudo-random 0..1 from two integers
  unsigned h = static_cast<unsigned>(a) * 374761393u + static_cast<unsigned>(b) * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return static_cast<double>((h ^ (h >> 16)) & 0xFFFF) / 65535.0;
}
double vnoise(double x, double y) {  // smooth value noise 0..1
  const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
  const double fx = x - ix, fy = y - iy, ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
  const double a = hash(ix, iy), b = hash(ix + 1, iy), c = hash(ix, iy + 1), d = hash(ix + 1, iy + 1);
  return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}
double bayer(int x, int y) {  // 4x4 ordered-dither threshold, 0..1
  static const int k[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  return (k[y & 3][x & 3] + 0.5) / 16.0;
}
double segDist(double px, double py, double ax, double ay, double bx, double by) {  // distance from a point to a segment
  const double dx = bx - ax, dy = by - ay;
  const double u = clamp01(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy));
  return std::hypot(px - ax - u * dx, py - ay - u * dy);
}

struct Tones {
  Rgb lit, body, shade, deep;  // the parchment in four steps, light from the right
  Rgb ink, seal, sealLit, sealDeep;
};

Tones tonesOf(const ScrollColors& p) {
  Tones t;
  t.body = p.parchment;
  t.lit = mix(p.parchment, kWhite, 0.35);
  t.shade = mix(p.parchment, p.ink, 0.30);  // shading and text go towards `ink`: the background on a dark theme, the text color on a light one
  t.deep = mix(p.parchment, p.ink, 0.55);
  t.ink = mix(p.parchment, p.ink, 0.72);
  t.seal = p.seal;
  t.sealLit = mix(p.seal, kWhite, 0.40);
  t.sealDeep = mix(p.seal, p.ink, 0.45);
  return t;
}

// A horizontal roll (cylinder) seen from the front: centre y `cy`, radius `r`, half length `len`; the top is lit.
bool horizontalRoll(const Tones& t, double nx, double ny, double cy, double r, double len, Rgb& out) {
  const double v = (ny - cy) / r;
  if (std::abs(v) > 1) {
    return false;
  }
  const double ex = std::abs(nx) - len;  // past the ends: the rolled-up paper's edge, a narrow ellipse
  if (ex > 0) {
    const double rx = r * 0.32;
    if (ex * ex / (rx * rx) + v * v > 1) return false;
    const double ring = std::hypot(ex / rx * 0.55, v);  // the spiral of the roll's edge
    out = nx > 0 ? t.lit : t.shade;
    if (std::fmod(ring * 3.2, 1.0) < 0.28) out = t.ink;
    return true;
  }
  out = v < -0.55 ? t.lit : v < 0.15 ? t.body : v < 0.65 ? t.shade : t.deep;
  if (std::abs(v) > 0.93) out = t.ink;  // the outline
  return true;
}

struct Sheet {
  double hw = 0.56, hh = 0.5;  // half width of the paper, half distance between the rolls
  double open = 1;             // 0 rolled shut .. 1 fully open
  double textShow = 1;         // how much of the text is written (top down)
  double rollExtra = 0;        // how much longer the roll is than the paper, on each side (in the sheet's own units)
};

// Cursive script for one line: words of loopy strokes with ascenders, descenders and little loops. `by` is the line's
// baseline, `h` its scale; the line spans x0..x0+span (only the first `fill` of it is written so far).
bool scriptInk(double nx, double ny, double by, double h, double x0, double span, double fill, int seed) {
  const double rel = nx - x0;
  if (rel < 0 || rel > span * fill || std::abs(ny - by) > 0.1 * h) return false;
  double wx = 0;
  for (int w = 0; w < 12 && wx < span; ++w) {
    const double ww = (0.16 + 0.25 * hash(w, seed)) * h / 0.8, end = std::min(wx + ww, span);
    if (rel >= wx && rel <= end) {
      const double u = (rel - wx) / (0.034 * h / 0.8), k = std::floor(u), fu = u - k;
      const double wave = by + 0.032 * h / 0.8 * std::sin(6.2832 * u + hash(w, seed + 1) * 3);
      if (std::abs(ny - wave) < 0.011) return true;                       // the joined baseline stroke
      const double cx = wx + (k + 0.5) * 0.034 * h / 0.8;
      const double hk = hash(static_cast<int>(k) + w * 13, seed + 2);
      const double slant = (by - ny) * 0.35;
      if (hk > 0.68 && ny < by + 0.01 && ny > by - 0.075 * h / 0.8 * (0.7 + 0.5 * hk) && std::abs(rel - cx - slant) < 0.009) return true;  // an ascender
      if (hk < 0.14 && ny > by - 0.01 && ny < by + 0.065 * h / 0.8 && std::abs(rel - cx + slant) < 0.009) return true;                 // a descender
      if (hk > 0.5 && hk < 0.68 && std::abs(std::hypot(rel - cx, (ny - by + 0.02) * 1.2) - 0.02) < 0.008) return true;                // a loop
      (void)fu;
    }
    wx = end + 0.05;
  }
  return false;
}

// The sheet scroll centred at (cx, cy) with scale `sc` (1 = fills about +-0.65): returns true where it paints. The paper is old:
// torn at the edges, bitten at a corner, holed, creased, stained and scorched along the borders.
bool sheetScroll(const Tones& t, const Sheet& s, double nx, double ny, double sc, Rgb& out) {
  nx /= sc;
  ny /= sc;
  const double hh = s.hh * s.open, rr = 0.16;
  // only the bottom is rolled; the top is a torn edge. The paper runs from there down to the roll.
  const double topEdge = -hh - 0.1 * s.open - 0.02 + (vnoise(nx * 10, 11) - 0.5) * 0.06 + (vnoise(nx * 27, 13) - 0.5) * 0.025;
  // the ragged side edges
  const double tearL = (vnoise(ny * 9, 1) - 0.5) * 0.035 + (vnoise(ny * 23, 5) - 0.5) * 0.012;
  const double tearR = (vnoise(ny * 8, 7) - 0.5) * 0.035 + (vnoise(ny * 21, 9) - 0.5) * 0.012;
  const double left = -s.hw + tearL + 0.01, right = s.hw + tearR - 0.01;
  const double bite = std::hypot(nx + s.hw, (ny - hh + 0.1) * 1.1) - (0.10 + 0.04 * vnoise(nx * 18 + ny * 5, 3));  // the lower left corner is nicked
  const double bite2 = std::hypot(nx - s.hw, ny - topEdge) - (0.06 + 0.03 * vnoise(nx * 15, ny * 15 + 4));          // a nibble at the upper right
  const double hole = std::hypot(nx - 0.2, (ny - 0.12) * 1.1) - (0.02 + 0.012 * vnoise(nx * 20, ny * 20));
  if (nx > left && nx < right && ny > topEdge && ny < hh - rr * 0.8 && bite > 0 && bite2 > 0 && hole > 0) {
    out = t.body;
    const double edge = std::min({nx - left, right - nx, ny - topEdge});
    // age: a few blotches of the shade tone, a darker rim along the edges
    const double blot = vnoise(nx * 4.5 + 2, ny * 4.5) * 0.65 + vnoise(nx * 11, ny * 11 + 6) * 0.35;
    if (blot > 0.74) out = t.shade;
    if (blot > 0.88) out = t.deep;
    if (edge < 0.015 + 0.015 * vnoise(ny * 17, 2)) out = t.shade;
    if (edge < 0.008) out = t.ink;
    // a fold crease across
    if (segDist(nx, ny, -s.hw, -0.2 * hh, s.hw, 0.1 * hh) < 0.007) out = t.shade;
    // the writing: a larger centred heading, then lines of script
    const double top = topEdge + 0.14, step = 0.145;
    const double f = (ny - top) / step;
    const int line = static_cast<int>(std::floor(f + 0.5));
    if (line >= 0 && line < 6 && hh > 0.2) {
      const double reveal = s.textShow * 7 - line, fill = clamp01(reveal);
      const double by = top + line * step + 0.02;
      const double margin = 0.12;
      const double span = line == 0 ? 0.38 : (line == 5 ? 0.4 : 1.0) * (s.hw * 2 - 2 * margin) - (line == 2 ? 0.1 : 0);
      const double x0 = line == 0 ? -span / 2 : -s.hw + margin;
      if (scriptInk(nx, ny, by, line == 0 ? 1.2 : 0.8, x0, span, fill, line * 11 + 1)) out = line == 0 ? t.seal : t.ink;
    }
    return true;
  }
  return horizontalRoll(t, nx, ny, hh, rr, s.hw - 0.04 + s.rollExtra, out);  // the roll is as wide as the paper (plus `rollExtra`)
}

// Dithered halo steps, as round the orb.
bool halo(const ScrollColors& p, double r, double inner, double px, double py, double strength, Rgb& out) {
  const double glow = std::exp(-std::max(0.0, r - inner) / 0.45) * strength;
  if (r > inner && glow > bayer(px, py) * 0.5 + std::floor(glow * 4) / 4 * 0.3) {
    out = mix(p.paper, p.glow, 0.18 + 0.25 * glow);
    return true;
  }
  return false;
}

void sparkle(const ScrollColors& p, double px, double py, double sx, double sy, double len, Rgb& out) {
  const double ax = std::abs(px - sx), ay = std::abs(py - sy);
  if ((ay < 0.6 && ax < len) || (ax < 0.6 && ay < len)) out = mix(p.glow, p.glint, 0.15);
}

}  // namespace

void drawScroll(CellGrid& g, int cx0, int cy0, int cols, int rows, int variant, const ScrollColors& pal, double t) {
  const int W = cols * 2, H = rows * 4;
  const double mx = W / 2.0, my = H / 2.0, S = std::min(W, H) / 2.0;
  const Tones tn = tonesOf(pal);
  const double pop = 1 - std::pow(1 - clamp01(t / 0.4), 3);  // 0..1, a quick ease-out for the arrival
  const double bob = std::sin(t * 2.2) * 0.02;

  const auto sample = [&](int px, int py) -> Rgb {
    const double fx = px + 0.5, fy = py + 0.5;
    const double nx = (fx - mx) / S, ny = (fy - my) / S;
    Rgb out = pal.paper;
    const double r = std::hypot(nx, ny);

    if (variant == 0) {  // ---- Sheet ----
      const double sc = 1.27 * (0.6 + 0.4 * pop);
      halo(pal, r, 0.55, fx, fy, 0.5 + 0.12 * std::sin(t * 2.6), out);
      Rgb o2 = out;
      Sheet sh;
      sh.rollExtra = 2.0 / (S * sc);  // one cell (two sub-pixels) on each side
      if (sheetScroll(tn, sh, nx, ny - bob, sc, o2)) out = o2;
    } else if (variant == 1) {  // ---- Tied ----
      // rays turning slowly behind
      const double ang = std::atan2(ny, nx);
      const double ray = std::pow(0.5 + 0.5 * std::cos(ang * 7 + t * 0.7), 2.5);
      if (r > 0.3 && ray * (1 - smooth(0.3, 0.98, r)) > 0.10 + 0.35 * bayer(px, py)) out = mix(pal.paper, pal.glow, 0.28);
      halo(pal, r, 0.5, fx, fy, 0.55, out);
      // the roll lies on a slant, rising to the right
      const double a = -0.5, ca = std::cos(a), sa = std::sin(a);
      const double dy = ny - bob - 0.03;
      const double s = nx * ca + dy * sa, q = -nx * sa + dy * ca;  // along and across the roll
      const double L = 0.62 * (0.7 + 0.3 * pop), R = 0.2;
      const double v = q / R;
      const double ex = std::abs(s) - L;
      if (ex <= 0 && std::abs(v) <= 1) {
        out = v > 0.4 ? tn.deep : v > -0.1 ? tn.shade : v > -0.6 ? tn.body : tn.lit;  // q grows toward lower left, so the top right is lit
        if (std::abs(v) > 0.92) out = tn.ink;
        // paper seams: faint rings across the roll
        if (std::abs(std::fmod(s + 4, 0.34) - 0.17) < 0.012 && std::abs(v) < 0.92) out = tn.shade;
      } else if (ex > 0) {
        const double rx = R * 0.34;
        const double ev = q / R;
        if (ex * ex / (rx * rx) + ev * ev <= 1) {
          out = s > 0 ? tn.lit : tn.shade;
          const double ring = std::hypot(ex / rx * 0.55, ev);
          if (std::fmod(ring * 3.4, 1.0) < 0.28) out = tn.ink;
          }
      }
      // two ribbon bands and a bow with a seal at the middle
      if (ex <= 0 && std::abs(v) <= 1) {
        for (const double bs : {-0.3, 0.3})
          if (std::abs(s - bs) < 0.045 && std::abs(v) < 1) out = std::abs(s - bs) > 0.03 || v > 0.5 ? tn.sealDeep : tn.seal;
      }
      // a bow on the front band: two loops and the knot
      const double bs2 = s - 0.3;
      for (const double side : {-1.0, 1.0}) {
        const double ox = bs2, oy2 = q - side * 0.17;
        if (ox * ox / (0.10 * 0.10) + oy2 * oy2 / (0.075 * 0.075) < 1) out = side < 0 ? tn.seal : tn.sealLit;
      }
      if (std::hypot(bs2, q) < 0.045) out = tn.sealDeep;
      // a wax seal at the middle of the roll
      if (std::hypot(s, q) < 0.15) {
        const double lx = s / 0.15, ly = q / 0.15, d = std::hypot(s, q);
        out = (-lx * 0.5 - ly * 0.6) > 0.4 ? tn.sealLit : (-lx * 0.5 - ly * 0.6) > -0.2 ? tn.seal : tn.sealDeep;
        if (d > 0.15 * 0.72 && d < 0.15 * 0.82) out = tn.sealDeep;
        if (d < 0.15 * 0.5 && (std::abs(lx) < 0.14 || std::abs(ly) < 0.14) && d > 0.15 * 0.1) out = tn.sealLit;
      }
    } else if (variant == 2) {  // ---- Circle ----
      const double rot = t * 0.25;
      // the circle: two rings, a ring of ticks, and a hexagram, each fading in with the arrival
      const double rc = 0.86 * (0.5 + 0.5 * pop);
      const double ringW = 0.018;
      const Rgb line = mix(pal.paper, pal.glow, 0.85), faint = mix(pal.paper, pal.glow, 0.45);
      if (std::abs(r - rc) < ringW * 1.4 || std::abs(r - rc * 0.9) < ringW * 0.7) out = std::abs(r - rc) < ringW * 1.4 ? line : faint;
      const double ang = std::atan2(ny, nx) - rot;
      if (r < rc * 0.9 && r > rc * 0.8 && std::fmod(ang / (2 * kPi) * 36 + 36, 1.0) < 0.16) out = faint;
      for (int tri = 0; tri < 2; ++tri) {
        double best = 9;
        for (int k = 0; k < 3; ++k) {
          const double a0 = rot + tri * kPi / 3 + k * 2 * kPi / 3 - kPi / 2, a1 = a0 + 2 * kPi / 3;
          best = std::min(best, segDist(nx, ny, std::cos(a0) * rc * 0.8, std::sin(a0) * rc * 0.8, std::cos(a1) * rc * 0.8, std::sin(a1) * rc * 0.8));
        }
        if (best < 0.024) out = tri ? line : faint;
      }
      halo(pal, r, 0.22, fx, fy, 0.5 + 0.12 * std::sin(t * 2.6), out);
      // the little glyphs between the points of the star
      for (int k = 0; k < 6; ++k) {
        const double a = rot + k * kPi / 3 - kPi / 2 + kPi / 6;
        if (std::hypot(nx - std::cos(a) * rc * 0.58 * 1.05, ny - std::sin(a) * rc * 0.58 * 1.05) < 0.035) out = line;
      }
      // the sheet hovers at the centre
      const Sheet sh{0.56, 0.42, 1, 1};
      Rgb o2 = out;
      if (sheetScroll(tn, sh, nx, ny - 0.02 - bob * 2.5, 0.62 * (0.6 + 0.4 * pop), o2)) out = o2;
      // a soft shadow below the floating sheet, on the circle
      const double sy = (ny - 0.62) / 0.07, sx = nx / 0.38;
      if (sx * sx + sy * sy < 1 && out.r == pal.paper.r && out.g == pal.paper.g && out.b == pal.paper.b) out = mix(pal.paper, pal.glow, 0.12);
    } else {  // ---- Unfurl ----
      const double open = smooth(0.05, 0.9, t);
      const double rr = 1 + 0.06 * std::sin(clamp01((t - 0.05) / 0.95) * kPi);  // a slight overshoot
      Sheet sh{0.56, 0.5, std::min(1.0, open * rr), smooth(0.7, 2.2, t)};
      const double burst = 1 - smooth(0.0, 0.5, t);
      halo(pal, r, 0.5, fx, fy, 0.5 + 0.12 * std::sin(t * 2.6) + 0.4 * burst, out);
      const double ringR = 0.5 + 1.0 * smooth(0.0, 0.8, t);
      if (t < 0.8 && std::abs(r - ringR) < 0.03 + 0.03 * (1 - t) && (1 - t / 0.8) > bayer(px + 1, py) * 0.9) out = mix(pal.glow, pal.glint, 0.5 * (1 - t / 0.8));
      const double oy = ny - bob;
      Rgb o2 = out;
      if (sheetScroll(tn, sh, nx, oy, 1.05, o2)) out = o2;
      // runes lifting off the page: tiny strokes rising, drifting, and fading out
      for (int i = 0; i < 10; ++i) {
        const double cyc = std::fmod(t * (0.16 + 0.12 * hash(i, 3)) + hash(i, 4), 1.0);
        if (t < 1.0 || cyc > 0.92) continue;
        const double rx = (hash(i, 1) * 2 - 1) * 0.5 + std::sin(t * 1.3 + i) * 0.04;
        const double ry = 0.35 - cyc * 1.05;
        const double ax = std::abs(nx - rx), ay = std::abs(ny - ry);
        const double fade = 1 - cyc;
        const int shape = static_cast<int>(hash(i, 5) * 3);
        const bool on = shape == 0 ? (ax < 0.012 && ay < 0.04) : shape == 1 ? (ay < 0.012 && ax < 0.035) || (ax < 0.012 && ay < 0.035) : std::abs(ax - ay) < 0.012 && ax < 0.035;
        if (on && fade > bayer(px, py) * 0.7) out = mix(pal.glow, pal.glint, 0.55);
      }
    }

    // sparkles (the sheet and the tied roll; the rune circle and the unfurl have their own motion)
    if (variant == 0 || variant == 1) {
      for (int i = 0; i < 8; ++i) {
        const double a = hash(i, 1) * 2 * kPi, rr = 0.62 + 0.3 * hash(i, 2);
        const double cyc = std::fmod(t * (0.35 + 0.3 * hash(i, 3)) + hash(i, 4), 1.0);
        const double tw = std::sin(cyc * kPi);
        if (t < 0.3 || tw < 0.15) continue;
        const double sx = mx + std::cos(a) * rr * S * 1.1, sy = my + std::sin(a) * rr * S * 0.85 - cyc * 5;
        sparkle(pal, fx, fy, sx, sy, 1.5 + 3.5 * tw, out);
      }
    }
    return out;
  };

  paintMosaic(g, cx0, cy0, cols, rows, sample);
}

}  // namespace laby
