#include "ui/OrbArt.hpp"

#include <algorithm>
#include <array>
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
double bayer(int x, int y) {  // 4x4 ordered-dither threshold, 0..1
  static const int k[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  return (k[y & 3][x & 3] + 0.5) / 16.0;
}

}  // namespace

void drawOrb(CellGrid& g, int cx0, int cy0, int cols, int rows, Rgb color, Rgb paper, double t) {
  const int W = cols * 2, H = rows * 4;
  const double mx = W / 2.0, my = H / 2.0;
  const double maxR = std::min(W, H) / 2.0 - 1;
  const double baseR = maxR * 0.8;

  // The orb pops in (a little overshoot), then breathes.
  const double u = clamp01(t / 0.45);
  const double pop = 1 + 2.2 * std::pow(u - 1, 3) + 1.2 * std::pow(u - 1, 2);  // easeOutBack: 0 -> 1 with overshoot
  const double R = baseR * std::max(0.0, pop) * (1 + 0.025 * std::sin(t * 4.0) * smooth(0.4, 0.8, t));
  const double shock = t < 0.9 ? t / 0.9 : 2.0;  // 0..1: the ring's progress
  const double flash = 1 - smooth(0.0, 0.35, t);  // white-hot at the start

  // On a light background the lit side still lifts towards white, but everything that sits on the bare paper (the rim, the
  // sparkles, the shock ring) would vanish if it went white, so those go darker instead; and the shadow side darkens rather
  // than fading to the (light) paper.
  const bool lightPaper = luminance(paper) > 0.4;
  const Rgb kBlack{0, 0, 0};
  const Rgb edge = lightPaper ? mix(color, kBlack, 0.55) : kWhite;  // what the paper-side highlights move towards
  const Rgb deep = lightPaper ? mix(color, kBlack, 0.50) : mix(paper, color, 0.28), shade = lightPaper ? mix(color, kBlack, 0.25) : mix(paper, color, 0.55);
  const Rgb body = color, lit = mix(color, kWhite, 0.45);
  const Rgb glass = mix(paper, color, 0.30);
  const Rgb rayCol = mix(paper, color, 0.50);

  struct Bubble { double x, spd, ph, size; };
  static const std::array<Bubble, 6> kBubbles{{{-0.45, 0.31, 0.0, 1.6}, {-0.15, 0.22, 0.4, 2.2}, {0.2, 0.27, 0.7, 1.4}, {0.45, 0.35, 0.2, 1.8}, {0.05, 0.18, 0.9, 1.2}, {-0.3, 0.25, 0.55, 1.3}}};

  const double surfaceBase = my - R * 0.05;  // the liquid's resting level (screen y grows downward)
  const double lightX = 0.55, lightY = -0.55, lightZ = 0.63;  // light from the upper right, as everywhere else

  const auto sample = [&](int px, int py) -> Rgb {
    const double dx = px + 0.5 - mx, dy = py + 0.5 - my;
    const double r = std::hypot(dx, dy);
    Rgb out = paper;

    // --- the halo: dithered steps of glow around the orb ---
    if (R > 1) {
      const double glow = std::exp(-std::max(0.0, r - R) / (baseR * 0.55)) * (0.55 + 0.15 * std::sin(t * 3.0));
      const double q = std::floor(glow * 4) / 4;  // flat steps, the dither softens their edges
      if (glow > bayer(px, py) * 0.5 + q * 0.3 && r > R) out = mix(paper, color, 0.20 + 0.25 * glow);

      // --- slow rotating rays ---
      const double ang = std::atan2(dy, dx);
      const double ray = std::pow(0.5 + 0.5 * std::cos(ang * 8 + t * 0.9), 2.5);
      const double reach = R * 1.6;
      const double fall = 1 - smooth(R * 1.05, reach, r);
      if (r > R * 1.05 && ray * fall * smooth(0.2, 0.7, t) > 0.12 + 0.35 * bayer(px, py)) out = rayCol;
    }

    // --- the shock ring ---
    if (shock < 1.5) {
      const double ringR = baseR * (0.6 + 1.6 * shock), width = 1.2 + 1.5 * (1 - shock);
      if (std::abs(r - ringR) < width && (1 - shock) > bayer(px + 1, py) * 0.9) out = mix(color, edge, 0.5 * (1 - shock));
    }

    // --- the sphere ---
    if (r < R) {
      const double nx = dx / R, ny = dy / R, nz = std::sqrt(std::max(0.0, 1 - nx * nx - ny * ny));
      const double d = nx * lightX + ny * lightY + nz * lightZ;  // -1..1
      const double wave = std::sin(px * 0.28 + t * 3.2) * 1.4 + std::sin(px * 0.11 - t * 2.1) * 1.0;
      const bool liquid = py + 0.5 > surfaceBase + wave;
      if (liquid) {
        out = d > 0.72 ? lit : d > 0.38 ? body : d > 0.0 ? shade : deep;
        // the meniscus: a bright line along the surface
        if (std::abs(py + 0.5 - (surfaceBase + wave)) < 1.1) out = mix(lit, kWhite, 0.35);
        // bubbles rising through the liquid
        for (const Bubble& b : kBubbles) {
          const double cyc = std::fmod(t * b.spd + b.ph, 1.0);
          const double bx = mx + b.x * R * 0.8 + std::sin(t * 2 + b.ph * 9) * 1.2;
          const double by = my + R * 0.78 - cyc * (R * 0.78 + (surfaceBase - my) + R * 0.05) * 1.0;
          if (by > surfaceBase + wave && std::hypot(px + 0.5 - bx, py + 0.5 - by) < b.size) out = mix(lit, kWhite, 0.5);
        }
      } else {
        out = d > 0.55 ? mix(glass, color, 0.35) : d > 0.0 ? glass : mix(paper, color, 0.20);  // the empty glass above the liquid
      }
      // the rim: a lighter line round the glass, thicker in the light
      if (r > R - 1.3) out = d > 0.0 ? (lightPaper ? mix(color, edge, 0.6) : mix(lit, kWhite, 0.3)) : mix(shade, lit, 0.4);
      // the specular highlights: a bar and a dot at the upper right
      const double hx = nx - 0.42, hy = ny + 0.48;
      if (hx * hx / 0.020 + hy * hy / 0.060 < 1 && ny < 0) out = kWhite;
      if (std::hypot(nx - 0.62, ny + 0.18) < 0.07) out = kWhite;
      // the opening flash
      if (flash > bayer(px, py + 1) * 0.9 + 0.1) out = mix(out, kWhite, 0.75);
    }

    // --- sparkles: four-point stars twinkling at fixed spots, each on its own beat ---
    for (int i = 0; i < 9; ++i) {
      const double a = hash(i, 1) * 2 * kPi, rr = baseR * (1.15 + 0.85 * hash(i, 2));
      const double cyc = std::fmod(t * (0.35 + 0.3 * hash(i, 3)) + hash(i, 4), 1.0);
      const double twinkle = std::sin(cyc * kPi);  // 0 .. 1 .. 0
      if (t < 0.3 || twinkle < 0.15) continue;
      const double sx = mx + std::cos(a) * rr * 1.25, sy = my + std::sin(a) * rr * 0.9 - cyc * 5;
      const double ax = std::abs(px + 0.5 - sx), ay = std::abs(py + 0.5 - sy);
      const double len = 1.5 + 3.5 * twinkle;
      if ((ay < 0.6 && ax < len) || (ax < 0.6 && ay < len)) out = lightPaper ? edge : mix(lit, kWhite, 0.6);
    }
    return out;
  };

  paintMosaic(g, cx0, cy0, cols, rows, sample);
}

}  // namespace laby
