// Prototype of the elevator ride scene in the Text style: the dungeon's own procedural cart (with a shake) over a scrolling
// ASCII shaft (ui/ShaftArt). Renders frames of the ride, going up (the scenery moves DOWN the picture) or down (it moves UP).
//   ride_sheet <outdir> [scale=3] [crt=1]
// Writes <outdir>/up.png and down.png (6 consecutive frames each, 12 units of scroll apart) and, for animating,
// <outdir>/frames/{up,down}_NN.png (50 frames each, 40 ms and 5 units apart).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "png_grid.hpp"

#include "core/DataFile.hpp"
#include "core/Tuning.hpp"
#include "platform/Theme.hpp"
#include "render/GlyphAtlas.hpp"
#include "ui/CorridorRenderer.hpp"
#include "ui/ScreenLayout.hpp"
#include "ui/ShaftArt.hpp"
#include "ui/ViewPainter.hpp"

using namespace laby;

namespace {
constexpr int kDensity = 2;
constexpr int kAnim = 50;  // animation frames, 40 ms apart  // the Text style: 4x8 sub-pixels per cell

Bitmap1 shifted(const Bitmap1& b, int dx, int dy) {
  Bitmap1 out(b.w, b.h);
  out.blit(b, dx, dy);
  return out;
}

struct Sheet {
  CellGrid grid;
  Sheet(int c, int r, Rgb bg) : grid(c, r) { grid.clear(bg); }
};
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: ride_sheet <outdir> [scale=3] [crt=1]\n");
    return 2;
  }
  const std::string dir = argv[1];
  const int scale = argc > 2 ? std::max(1, std::atoi(argv[2])) : 3;
  const int crt = argc > 3 ? std::atoi(argv[3]) : 1;
  const int vpCols = kViewportCols, vpRows = kViewportRows, w = vpCols * 2 * kDensity, h = vpRows * 4 * kDensity;
  const Theme th = loadTheme();
  const ObjectColors oc = objectColors(th);

  // the cart, traced once from outside (as in the dungeon, standing one cell back), Text-style density
  const auto& v = tuning().view;
  const TileLookup lookup = [&](int a, int r) {
    TileInfo t;
    t.mx = r;
    t.my = -a;
    t.tile = (r < -2 || r > 2 || a > 7) ? TileClass::WALL : TileClass::FLOOR;
    if (a == 1 && r == 0) t.tile = TileClass::ELEVATOR;
    if (a == 0 && r == 0) t.tile = TileClass::PLAYER;
    return t;
  };
  CorridorCamera cam;
  cam.fovDegrees = v.fovDegrees;
  cam.eyeHeight = v.eyeHeight;
  cam.drawDistance = v.drawDistance;
  cam.fog = v.fog;
  cam.supersample = std::max(1, v.supersample / kDensity);
  cam.lineScale = kDensity;
  ViewLayers cart = toLayers(renderCorridor(w, h, lookup, cam));
  cart.density = kDensity;
  cart.background = Bitmap1(w, h);  // the dungeon's floor, ceiling and walls are not part of the scene
  cart.walls = Bitmap1(w, h);

  const auto fonts = findFonts(dataDir() + "/fonts");
  std::vector<FontFace> faces(fonts.size());
  std::vector<const FontFace*> chain;
  for (size_t i = 0; i < fonts.size(); ++i)
    if (faces[i].load(fonts[i].path)) chain.push_back(&faces[i]);
  const int cellH = scale * 4;
  const Atlas atlas = buildGlyphAtlas(cellH / 2, cellH, chain, 11);

  const auto frameLayers = [&](double scrollUnits, double ms) {
    const CartShake s = cartShake(ms);
    ViewLayers c = cart;
    c.objects = shifted(c.objects, s.dx, s.dy);
    c.shade = shifted(c.shade, s.dx, s.dy);
    for (auto& t : c.tints) {
      if (t.lit.w) t.lit = shifted(t.lit, s.dx, s.dy);
      if (t.shade.w) t.shade = shifted(t.shade, s.dx, s.dy);
    }
    ViewLayers L = c;
    L.shade = L.background = L.walls = Bitmap1(w, h);
    paintShaft(L, scrollUnits);
    shadeBehindCart(L, c);
    L.shade.blit(c.shade, 0, 0);
    return L;
  };

  system(("mkdir -p '" + dir + "/frames'").c_str());
  for (const bool up : {true, false}) {
    const char* name = up ? "up" : "down";
    const double sign = up ? 1 : -1;  // going up: the cart rises, so the shaft slides DOWN the picture
    // contact sheet: 6 consecutive frames, 2 x 3
    {
      const int cols = 2, rows = 3, gc = cols * (vpCols + 1) + 1, gr = rows * (vpRows + 2) + 1;
      CellGrid grid(gc, gr);
      grid.clear(th.bg);
      for (int i = 0; i < 6; ++i) {
        const int cx = 1 + (i % cols) * (vpCols + 1), cy = 1 + (i / cols) * (vpRows + 2);
        drawViewLayers(grid, cx, cy, frameLayers(sign * 12 * i, i * 120.0), th.accent, th.bg, nullptr, &oc);
        char label[96];
        std::snprintf(label, sizeof label, "[ %s  frame %d of 6  scroll %+d ]", up ? "GOING UP: scenery moves DOWN" : "GOING DOWN: scenery moves UP", i + 1, static_cast<int>(sign * 12 * i));
        grid.print(cx + 1, cy + vpRows, label, th.accent, th.bg);
      }
      writeGridPng((dir + "/" + name + ".png").c_str(), grid, atlas, crt);
    }
    // animation frames
    for (int i = 0; i < kAnim; ++i) {
      CellGrid grid(vpCols + 2, vpRows + 2);
      grid.clear(th.bg);
      drawViewLayers(grid, 1, 1, frameLayers(sign * 5 * i, i * 40.0), th.accent, th.bg, nullptr, &oc);
      char path[512];
      std::snprintf(path, sizeof path, "%s/frames/%s_%02d.png", dir.c_str(), name, i);
      writeGridPng(path, grid, atlas, crt);
    }
  }
  return 0;
}
