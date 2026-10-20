// The art workbench: renders the procedural objects (elevator, chests, boss lair) from several distances and angles
// into one contact-sheet PNG, with no HUD, so a change to the art in CorridorRenderer.cpp can be judged in seconds.
// It draws through the game's own path (drawViewLayers: octant text cells with the theme's colours, two colours to a cell),
// so what you see here is what the game shows (apart from the CRT filter, which is optional).
//   art_sheet <out.png> [set=all|elevator|chest|lair|close|zoom|side] [scale=4] [ss=2] [crt=0]
//   scale: screen pixels per picture pixel (even cell height = 4 x scale); ss: the game's view.supersample (2); crt: 0 = off, 1..3 = the game's levels
// tools/art.sh wraps this (rebuild, render, and `watch` to redo it on every save).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "png_grid.hpp"

#include "core/DataFile.hpp"
#include "core/Enums.hpp"
#include "core/Tuning.hpp"
#include "platform/Theme.hpp"
#include "render/GlyphAtlas.hpp"
#include "ui/CorridorRenderer.hpp"
#include "ui/ScreenLayout.hpp"
#include "ui/ViewPainter.hpp"

using namespace laby;

namespace {
struct Scene {
  const char* set;
  TileClass tile;  // the object's tile (placed at ahead, right), or FLOOR for none
  int ahead, right;
  int held;  // CorridorCamera::heldObject
  bool corridor = false;  // a one-wide corridor with the object in its dead end (as in the game), instead of open floor
};
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: art_sheet <out.png> [all|elevator|deadend|treasure|chest|lair|close|zoom|side] [scale=4] [ss=2] [crt=0]\n");
    return 2;
  }
  const std::string which = argc > 2 ? argv[2] : "all";
  const int scale = argc > 3 ? std::max(1, std::atoi(argv[3])) : 4;
  const int ss = argc > 4 ? std::max(1, std::atoi(argv[4])) : tuning().view.supersample;
  const int crt = argc > 5 ? std::atoi(argv[5]) : 0;
  using T = TileClass;
  const std::vector<Scene> scenes = {
      {"elevator", T::ELEVATOR, 1, 0, 0}, {"elevator", T::ELEVATOR, 2, 0, 0}, {"elevator", T::ELEVATOR, 3, 0, 0},
      {"elevator", T::ELEVATOR, 2, 1, 0}, {"elevator", T::ELEVATOR, 2, -1, 0}, {"elevator", T::ELEVATOR, 5, 0, 0},
      {"elevator", T::ELEVATOR, 1, 1, 0}, {"elevator", T::ELEVATOR, 1, -1, 0},
      {"treasure", T::TREASURE, 1, 0, 0}, {"treasure", T::TREASURE, 1, 1, 0}, {"treasure", T::TREASURE, 1, -1, 0}, {"treasure", T::FLOOR, 0, 0, 3},
      {"chest", T::TREASURE, 2, 0, 0},    {"chest", T::TREASURE, 3, 1, 0},     {"chest", T::FLOOR, 0, 0, 1},
      {"deadend", T::ELEVATOR, 1, 0, 0, true}, {"deadend", T::ELEVATOR, 2, 0, 0, true}, {"deadend", T::ELEVATOR, 3, 0, 0, true},
      {"chest", T::FLOOR, 0, 0, 3},       {"lair", T::BOSS, 1, 0, 0},          {"lair", T::BOSS, 2, 0, 0},          {"lair", T::BOSS, 3, 0, 0},
      {"lair", T::BOSS, 1, 1, 0},          {"lair", T::BOSS, 1, -1, 0},         {"lair", T::BOSS, 2, 1, 0},          {"lair", T::BOSS, 1, 0, 0, true}, {"lair", T::BOSS, 2, 0, 0, true},
      {"floor", T::FLOOR, 7, 0, 0, true}, {"floor", T::FLOOR, 0, 0, 0, false},  // floor and ceiling: a long corridor, and an open hall
  };
  const bool intro = which == "intro";  // the boss-encounter animation: a few of its 27 frames, on the original's white canvas
  std::vector<Scene> pick;
  for (const Scene& s : scenes)
    if (which == "all" || which == s.set || (which == "chest" && std::string(s.set) == "treasure") || (which == "close" && s.ahead <= 2 && s.right == 0) ||
        (which == "deadend" && std::string(s.set) == "deadend" && false) ||
        (which == "zoom" && std::string(s.set) == "elevator" && s.ahead <= 2 && s.right == 0) ||
        (which == "side" && std::string(s.set) == "elevator" && s.right != 0 && s.ahead <= 2))
      pick.push_back(s);
  if (intro) pick.assign(8, scenes[0]);
  if (pick.empty()) return 2;

  // the picture is drawn into a grid of cells, several pictures per sheet, with a one-cell gap
  const int vpCols = kViewportCols, vpRows = kViewportRows, w = vpCols * 2, h = vpRows * 4;
  const int cols = pick.size() >= 6 ? 2 : 1, rows = (static_cast<int>(pick.size()) + cols - 1) / cols;
  const int gridCols = cols * (vpCols + 1) + 1, gridRows = rows * (vpRows + 1) + 1;
  const WallView wallView = WallView::fromDataFile();
  const ViewPainter painter(wallView);
  const Theme th = loadTheme();
  const ObjectColors oc = objectColors(th);
  CellGrid grid(gridCols, gridRows);
  grid.clear(th.bg);

  for (size_t i = 0; i < pick.size(); ++i) {
    const Scene& s = pick[i];
    if (intro) {
      static const int kFrames[8] = {1, 5, 9, 13, 17, 21, 24, 27};
      ViewLayers L{Bitmap1(w, h), Bitmap1(w, h), Bitmap1(w, h), Bitmap1(w, h)};
      L.native = true;
      std::fill(L.walls.px.begin(), L.walls.px.end(), Bitmap1::Light);
      L.blitOriginal(L.objects, painter.image("Monsters/LevelBoss_Encounter-table-" + std::to_string(kFrames[i])), 0, 0);
      drawViewLayers(grid, 1 + (static_cast<int>(i) % cols) * (vpCols + 1), 1 + (static_cast<int>(i) / cols) * (vpRows + 1), L, th.accent, th.bg, nullptr, &oc);
      continue;
    }
    const TileLookup lookup = [&](int a, int r) {
      TileInfo t;
      t.mx = r;
      t.my = -a;
      t.tile = (r < -2 || r > 2 || a > 7) ? TileClass::WALL : TileClass::FLOOR;
      if (s.corridor && (r != 0 || a > s.ahead || a < 0)) t.tile = TileClass::WALL;
      if (a == s.ahead && r == s.right && s.tile != T::FLOOR) t.tile = s.tile;
      if (a == 0 && r == 0) t.tile = TileClass::PLAYER;
      return t;
    };
    CorridorCamera cam;
    const auto& v = tuning().view;  // the game's own camera (fov, eye height, fog ...)
    cam.fovDegrees = v.fovDegrees;
    cam.eyeHeight = v.eyeHeight;
    cam.drawDistance = v.drawDistance;
    cam.fog = v.fog;
    cam.supersample = ss;
    cam.heldObject = s.held;
    cam.lair = &painter.lair();
    const ViewLayers L = toLayers(renderCorridor(w, h, lookup, cam));
    drawViewLayers(grid, 1 + (static_cast<int>(i) % cols) * (vpCols + 1), 1 + (static_cast<int>(i) / cols) * (vpRows + 1), L, th.accent, th.bg, nullptr, &oc);
  }

  const auto fonts = findFonts(dataDir() + "/fonts");
  std::vector<FontFace> faces(fonts.size());
  std::vector<const FontFace*> chain;
  for (size_t i = 0; i < fonts.size(); ++i)
    if (faces[i].load(fonts[i].path)) chain.push_back(&faces[i]);
  const int cellH = scale * 4;
  const Atlas atlas = buildGlyphAtlas(cellH / 2, cellH, chain, 11);
  writeGridPng(argv[1], grid, atlas, crt);
  return 0;
}
