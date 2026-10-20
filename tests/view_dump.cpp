// Renders one first-person view at a chosen viewport width (the original is 241 px = 121 cells, ours is 100 cells), to
// compare how the art fits.   view_dump <row0> <row1> <row2> <out.png>
// Rows are three tile characters each (left, centre, right), e.g.  "#@>" ".#."  "###"
#include <cstdio>
#include <cstdlib>
#include <string>

#include "png_grid.hpp"

#include "core/DataFile.hpp"
#include "game/WallView.hpp"
#include "render/GlyphAtlas.hpp"
#include "ui/MainScreen.hpp"
#include "ui/ScreenLayout.hpp"
#include "ui/CorridorRenderer.hpp"
#include "ui/ViewPainter.hpp"

using namespace laby;

int main(int argc, char** argv) {
  if (argc < 5) {
    std::fprintf(stderr, "usage: view_dump <row0> <row1> <row2> <out.png>\n");
    return 2;
  }
  WallView wv = WallView::fromDataFile();
  ViewPainter painter(wv);
  WallView::Input in;
  auto row = [&](const char* s) { return std::array<TileClass, 3>{static_cast<TileClass>(s[0]), static_cast<TileClass>(s[1]), static_cast<TileClass>(s[2])}; };
  in.row0 = row(argv[1]);
  in.row1 = row(argv[2]);
  in.row2 = row(argv[3]);
  const std::array<std::array<TileClass, 3>, 3> rows = {in.row0, in.row1, in.row2};
  const TileLookup lookup = [&](int a, int r) {
    TileInfo t;
    t.mx = r;
    t.my = -a;  // facing north from (0, 0)
    t.tile = (a >= 0 && a <= 2 && r >= -1 && r <= 1) ? rows[static_cast<size_t>(a)][static_cast<size_t>(r + 1)] : TileClass::WALL;
    if (a == 0 && r == 0) t.tile = TileClass::PLAYER;
    return t;
  };
  ViewLayers layers;
  CorridorCamera cam;
  if (const char* e = std::getenv("VIEW")) std::sscanf(e, "%lf,%lf,%lf", &cam.fovDegrees, &cam.drawDistance, &cam.fog);  // VIEW=fov,dist,fog
  if (const char* e = std::getenv("HELD")) cam.heldObject = std::atoi(e);
  cam.lair = &painter.lair();
  if (const char* e = std::getenv("SS")) cam.supersample = std::atoi(e);
  layers = toLayers(renderCorridor(kViewportCols * 2, kViewportRows * 4, lookup, cam));

  const auto fonts = findFonts(dataDir() + "/fonts");
  std::vector<FontFace> faces(fonts.size());
  std::vector<const FontFace*> chain;
  for (size_t i = 0; i < fonts.size(); ++i)
    if (faces[i].load(fonts[i].path)) chain.push_back(&faces[i]);
  const int cellH = std::getenv("CELL") ? std::atoi(std::getenv("CELL")) & ~1 : 12;  // CELL=24 for a bigger picture
  const Atlas atlas = buildGlyphAtlas(cellH / 2, cellH, chain, 11);
  CellGrid grid(kGridCols, kGridRows);
  MainScreenOptions opt;
  opt.view = &layers;
  drawMainScreen(grid, loadTheme(), opt);
  writeGridPng(argv[4], grid, atlas, 2);
}
