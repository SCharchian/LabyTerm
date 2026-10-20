// Invariants of the procedural corridor renderer (src/ui/CorridorRenderer.*).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/Rng.hpp"
#include "ui/CorridorRenderer.hpp"

using namespace laby;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);             \
      return 1;                                                           \
    }                                                                     \
  } while (0)

// A small map given as text rows; the player is at `px, py` facing north (up the rows).
struct Grid {
  std::vector<std::string> rows;
  int px = 0, py = 0;
  TileLookup lookup() const {
    return [this](int a, int r) {
      TileInfo t;
      t.mx = px + r;
      t.my = py - a;
      t.tile = TileClass::WALL;
      if (t.my >= 0 && t.my < static_cast<int>(rows.size()) && t.mx >= 0 && t.mx < static_cast<int>(rows[static_cast<size_t>(t.my)].size()))
        t.tile = static_cast<TileClass>(rows[static_cast<size_t>(t.my)][static_cast<size_t>(t.mx)]);
      if (a == 0 && r == 0) t.tile = TileClass::PLAYER;
      return t;
    };
  }
  CorridorCamera camera() const {
    CorridorCamera c;
    c.frame = {px, py, 0, -1, 1, 0};
    return c;
  }
};

int main() {
  // 1. In a room closed by walls (smaller than the draw distance) nothing is "void": every pixel shows a surface.
  Rng rng(7);
  const char kTiles[] = ".....+'>BT#";
  for (int n = 0; n < 60; ++n) {
    Grid g;
    const int w = 5 + static_cast<int>(rng.range(0, 3)), h = 5 + static_cast<int>(rng.range(0, 3));
    for (int y = 0; y < h; ++y) {
      std::string row;
      for (int x = 0; x < w; ++x) {
        const bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
        row += edge ? '#' : kTiles[rng.range(0, sizeof kTiles - 2)];
      }
      g.rows.push_back(row);
    }
    g.px = w / 2;
    g.py = h / 2;
    g.rows[static_cast<size_t>(g.py)][static_cast<size_t>(g.px)] = '@';
    const ViewFrame f = renderCorridor(120, 68, g.lookup(), g.camera());
    int voids = 0;
    for (const Fragment& p : f.px) voids += p.surface == Surface::Void;
    if (voids) {
      std::printf("room %d (%dx%d): %d void pixels\n", n, w, h, voids);
      for (const auto& r : g.rows) std::printf("  %s\n", r.c_str());
      return 1;
    }
  }

  // 2. A symmetric map gives a mirror-symmetric picture (which surface is where), and rendering is deterministic.
  Grid sym;
  sym.rows = {"#######", "#.....#", "#.>.>.#", "#..@..#", "#######"};
  sym.rows = {"#########", "#...B...#", "#.......#", "#...@...#", "#########"};  // (no elevators or level treasures: the elevator has its doorway on one side, and the light comes from the right)
  sym.px = 4;
  sym.py = 3;
  const ViewFrame a = renderCorridor(100, 56, sym.lookup(), sym.camera());
  const ViewFrame b = renderCorridor(100, 56, sym.lookup(), sym.camera());
  for (int y = 0; y < a.h; ++y)
    for (int x = 0; x < a.w; ++x) {
      CHECK(a.at(x, y).surface == b.at(x, y).surface && a.at(x, y).albedo == b.at(x, y).albedo);
      CHECK(a.at(x, y).surface == a.at(a.w - 1 - x, y).surface);
    }

  // 3. A wall right in front of you (the next cell) is half a cell away; the floor under your feet is closer.
  Grid wall;
  wall.rows = {"###", "#.#", "#@#"};
  wall.px = 1;
  wall.py = 2;
  const ViewFrame wf = renderCorridor(100, 56, wall.lookup(), wall.camera());
  const Fragment& mid = wf.at(50, 28);
  CHECK(mid.surface == Surface::Walls);
  CHECK(std::fabs(mid.depth - 1.5) < 0.02);  // the wall of the cell two ahead: its face is at 1.5
  CHECK(wf.at(50, 55).surface == Surface::Background && wf.at(50, 55).depth < 1.0f);  // floor at the bottom
  CHECK(wf.at(50, 0).surface == Surface::Background);                                  // ceiling at the top

  // 4. Resolution independence: the same view at twice the size shows the same surfaces (apart from thin edges).
  Grid mix;
  mix.rows = {"#########", "#...#...#", "#.#.'.>.#", "#...+...#", "#...@...#", "#########"};
  mix.px = 4;
  mix.py = 4;
  const ViewFrame lo = renderCorridor(100, 56, mix.lookup(), mix.camera());
  const ViewFrame hi = renderCorridor(200, 112, mix.lookup(), mix.camera());
  int differ = 0;
  for (int y = 0; y < lo.h; ++y)
    for (int x = 0; x < lo.w; ++x) differ += lo.at(x, y).surface != hi.at(2 * x, 2 * y).surface;
  CHECK(differ * 100 < lo.w * lo.h * 2);  // under 2% of pixels

  // 5. The elevator cart beside you runs to the edge of the picture: no gap of void next to it (the bug that started this)
  Grid cart;
  cart.rows = {"#####", "#..>#", "#.@.#", "#####"};
  cart.px = 2;
  cart.py = 2;
  const ViewFrame cf = renderCorridor(200, 112, cart.lookup(), cart.camera());
  for (int y = 0; y < cf.h; ++y)
    for (int x = cf.w - 20; x < cf.w; ++x) CHECK(cf.at(x, y).surface != Surface::Void);

  std::printf("corridor renderer: all checks passed\n");
  return 0;
}
