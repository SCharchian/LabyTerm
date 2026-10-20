#include "ui/MapGlyphs.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "render/GlyphAtlas.hpp"

namespace laby {
namespace {

struct Pt { double x, y; };

bool inTriangle(Pt p, Pt a, Pt b, Pt c) {
  const auto s = [](Pt p1, Pt p2, Pt p3) { return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y); };
  const double d1 = s(p, a, b), d2 = s(p, b, c), d3 = s(p, c, a);
  const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
  return !(neg && pos);
}
bool inRect(Pt p, double x0, double y0, double x1, double y1) { return p.x >= x0 && p.x < x1 && p.y >= y0 && p.y < y1; }
bool inDisc(Pt p, double cx, double cy, double r) { return (p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy) <= r * r; }

struct Palette {
  Rgb bg, wall, floor, door, water, dry, lift, boss, chest, hero;
};

// The colour of the point (u, v) in a tile (both 0..1, y down). `px` is one sub-pixel in tile units, for line widths.
Rgb sample(const Palette& p, MapTile t, int facing, Pt q, double px) {
  const auto out = [](Rgb c) { return c; };
  const Pt c{0.5, 0.5};
  switch (t) {
    case MapTile::Unseen: return p.bg;
    case MapTile::Wall: return out(p.wall);
    case MapTile::Floor: return out(p.floor);
    case MapTile::DoorClosed:  // a shut panel: solid (the open door is the same frame, hollow)
      return inRect(q, 0.16, 0.12, 0.84, 0.88) ? out(p.door) : out(p.floor);
    case MapTile::DoorOpen: {  // the same frame, empty: the panel has swung open
      const bool ring = inRect(q, 0.16, 0.12, 0.84, 0.88) && !inRect(q, 0.16 + px * 1.1, 0.12 + px * 1.1, 0.84 - px * 1.1, 0.88 - px * 1.1);
      return ring ? out(p.door) : out(p.floor);
    }
    case MapTile::Fountain:
    case MapTile::FountainDry: {  // a drop
      const bool drop = inTriangle(q, {0.5, 0.04}, {0.2, 0.6}, {0.8, 0.6}) || inDisc(q, 0.5, 0.64, 0.31);
      const bool basin = false;
      return drop || basin ? out(t == MapTile::Fountain ? p.water : p.dry) : out(p.floor);
    }
    case MapTile::Elevator: {  // a wireframe cabin: outline, the doors' seam, a floor line
      const double w = std::max(px, 0.1), m = 0.14;
      const bool box = inRect(q, m, 0.06, 1 - m, 0.94) && !inRect(q, m + w, 0.06 + w, 1 - m - w, 0.94 - w);
      const bool seam = inRect(q, 0.5 - w / 2, 0.06, 0.5 + w / 2, 0.94);
      return box || seam ? out(p.lift) : out(p.floor);
    }
    case MapTile::Boss: {  // a skull
      const bool cranium = inDisc(q, 0.5, 0.42, 0.36), jaw = inRect(q, 0.3, 0.6, 0.7, 0.9);
      const bool eyes = inDisc(q, 0.35, 0.44, 0.1) || inDisc(q, 0.65, 0.44, 0.1);
      const bool teeth = inRect(q, 0.46, 0.74, 0.54, 0.9);
      return (cranium || jaw) && !eyes && !teeth ? out(p.boss) : out(p.floor);
    }
    case MapTile::Chest: {  // a chest: lid, body, lock
      const bool body = inRect(q, 0.12, 0.3, 0.88, 0.82);
      const bool band = inRect(q, 0.12, 0.5 - px * 0.5, 0.88, 0.5 + px * 0.5);
      const bool lock = inRect(q, 0.5 - px * 0.7, 0.44, 0.5 + px * 0.7, 0.66);
      if (!body) return out(p.floor);
      return band || lock ? out(mix(p.floor, p.chest, 0.15)) : out(p.chest);
    }
    case MapTile::Player: {  // a solid arrow, turned to face
      Pt r{q.x - c.x, q.y - c.y};  // un-rotate the sample so the shape is drawn pointing up
      for (int i = 0; i < facing; ++i) r = {r.y, -r.x}; // 90 degrees back, per quarter turn
      const Pt u{r.x + 0.5, r.y + 0.5};
      const double notch = px < 0.15 ? 0.76 : 0.94;  // a notched head needs 8 sub-pixels; smaller is a plain triangle
      const bool arrow = inTriangle(u, {0.5, 0.02}, {0.94, 0.9}, {0.5, notch}) || inTriangle(u, {0.5, 0.02}, {0.06, 0.9}, {0.5, notch});
      return arrow ? p.hero : out(p.floor);
    }
  }
  return p.bg;
}

// Hand-drawn pictograms for the two small sizes: '#' is the tile's ink, '.' its floor.
using Art = std::array<const char*, 6>;
const Art* artFor(MapTile t, int size) {
  static const Art k6[] = {
      {".####.", ".####.", ".####.", ".####.", ".####.", ".####."},  // closed door
      {".####.", ".#..#.", ".#..#.", ".#..#.", ".#..#.", ".####."},  // open door
      {"..##..", "..##..", ".####.", "######", "######", ".####."},  // fountain
      {"######", "#.#..#", "#.#..#", "#.#..#", "#.#..#", "######"},  // elevator cabin (as the size-8 drawing: outline, one-pixel seam)
      {".####.", "######", "#.##.#", "######", ".####.", ".#..#."},  // skull
      {"......", "######", "######", "##..##", "######", "######"},  // chest
      {"..##..", ".####.", ".####.", "######", "######", "##..##"},  // arrow pointing north
  };
  static const Art k4[] = {
      {"####", "####", "####", "####"},
      {"####", "#..#", "#..#", "####"},
      {".##.", "####", "####", ".##."},
      {"####", "#.##", "#.##", "####"},
      {"####", "#..#", "####", ".##."},
      {"....", "####", "#..#", "####"},
      {".##.", ".##.", "####", "####"},
  };
  int i = -1;
  switch (t) {
    case MapTile::DoorClosed: i = 0; break;
    case MapTile::DoorOpen: i = 1; break;
    case MapTile::Fountain: case MapTile::FountainDry: i = 2; break;
    case MapTile::Elevator: i = 3; break;
    case MapTile::Boss: i = 4; break;
    case MapTile::Chest: i = 5; break;
    case MapTile::Player: i = 6; break;
    default: return nullptr;
  }
  return size == 6 ? &k6[i] : size == 4 ? &k4[i] : nullptr;
}

Rgb inkOf(const Palette& p, MapTile t) {
  switch (t) {
    case MapTile::DoorClosed: case MapTile::DoorOpen: return p.door;
    case MapTile::Fountain: return p.water;
    case MapTile::FountainDry: return p.dry;
    case MapTile::Elevator: return p.lift;
    case MapTile::Boss: return p.boss;
    case MapTile::Chest: return p.chest;
    default: return p.hero;
  }
}

int dist(Rgb a, Rgb b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); }

}  // namespace

void drawMapPicture(CellGrid& g, const Theme& th, Rgb bg, int cx, int cy, const MapPicture& map, int size) {
  Palette p;
  p.bg = bg;
  p.wall = mix(bg, th.frame, 0.5);
  p.floor = mix(bg, th.frame, 0.22);
  p.door = th.fg;
  p.water = th.mana;
  p.dry = mix(p.floor, th.mana, 0.4);
  p.lift = th.mana;
  p.boss = th.danger;
  p.chest = th.coin;
  p.hero = th.good;
  const int W = map.cols * size, H = map.rows * size, cw = (W + 1) / 2, ch = (H + 3) / 4;
  std::vector<Rgb> px(static_cast<size_t>(cw * 2 * ch * 4), bg);
  const auto at = [&](int x, int y) -> Rgb& { return px[static_cast<size_t>(y * cw * 2 + x)]; };
  const double one = 1.0 / size;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const int tc = x / size, tr = y / size;
      const size_t i = static_cast<size_t>(tr * map.cols + tc);
      const Pt q{(x % size + 0.5) / size, (y % size + 0.5) / size};
      const MapTile t = map.tile[i];
      if (const Art* art = artFor(t, size)) {
        int ax = x % size, ay = y % size;
        if (t == MapTile::Player)  // turn the north-pointing arrow: sample where this pixel comes from
          for (int k = 0; k < map.facing; ++k) { const int nx = ay, ny = size - 1 - ax; ax = nx; ay = ny; }
        at(x, y) = (*art)[static_cast<size_t>(ay)][ax] == '#' ? inkOf(p, t) : p.floor;
      } else {
        at(x, y) = sample(p, t, map.facing, q, one);
      }
    }
  for (int row = 0; row < ch; ++row)
    for (int col = 0; col < cw; ++col) {
      std::array<Rgb, 8> c;
      for (int k = 0; k < 8; ++k) c[static_cast<size_t>(k)] = at(col * 2 + k % 2, row * 4 + k / 2);
      // The two colours: the commonest, and the one farthest from it among the rest.
      Rgb a = c[0];
      int best = 0;
      for (Rgb x : c) {
        int n = 0;
        for (Rgb y : c) n += x == y;
        if (n > best) { best = n; a = x; }
      }
      Rgb b = a;
      int far = 0;
      for (Rgb x : c)
        if (dist(x, a) > far) { far = dist(x, a); b = x; }
      unsigned pattern = 0;
      for (int k = 0; k < 8; ++k)
        if (dist(c[static_cast<size_t>(k)], b) < dist(c[static_cast<size_t>(k)], a)) pattern |= 1u << k;
      g.put(cx + col, cy + row, pattern ? mosaicGlyph(pattern) : U' ', b, a);
    }
}

}  // namespace laby
