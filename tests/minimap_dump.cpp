// The minimap's pictograms at real size, in a row per tile size (8, 6, 4), with the player arrow facing each way.
//   minimap_dump <out.png> [cellH=12] [crt=0]
#include <cstdio>
#include <cstdlib>

#include "png_grid.hpp"

#include "core/DataFile.hpp"
#include "render/GlyphAtlas.hpp"
#include "ui/MapGlyphs.hpp"

using namespace laby;

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: minimap_dump <out.png> [cellH] [crt]\n"); return 2; }
  const int cellH = (argc > 2 ? std::atoi(argv[2]) : 12) & ~1, crt = argc > 3 ? std::atoi(argv[3]) : 0;
  const auto fonts = findFonts(dataDir() + "/fonts");
  std::vector<FontFace> faces(fonts.size());
  std::vector<const FontFace*> chain;
  for (size_t i = 0; i < fonts.size(); ++i)
    if (faces[i].load(fonts[i].path)) chain.push_back(&faces[i]);
  const Atlas atlas = buildGlyphAtlas(cellH / 2, cellH, chain, 11);
  const Theme th = loadTheme();
  CellGrid grid(80, 20);
  grid.fill(0, 0, 80, 20, U' ', th.fg, th.bg);
  const MapTile kinds[] = {MapTile::Wall, MapTile::Floor, MapTile::DoorClosed, MapTile::DoorOpen, MapTile::Fountain, MapTile::FountainDry, MapTile::Elevator, MapTile::Boss, MapTile::Chest};
  const char* names[] = {"wall", "floor", "door", "open", "fount", "dry", "lift", "boss", "chest"};
  int y = 1;
  for (int size : {8, 6, 4}) {
    int x = 2;
    const int w = mapCellsWide(1, size);
    for (size_t i = 0; i < 9; ++i, x += w + 3) {
      MapPicture m;
      m.cols = m.rows = 1;
      m.tile = {kinds[i]};
      drawMapPicture(grid, th, th.bg, x, y + 1, m, size);
      if (size == 8) grid.print(x, y, names[i], th.muted, th.bg);
    }
    for (int f = 0; f < 4; ++f, x += w + 1) {
      MapPicture a;
      a.cols = a.rows = 1;
      a.tile = {MapTile::Player};
      a.facing = f;
      drawMapPicture(grid, th, th.bg, x, y + 1, a, size);
    }
    y += 6;
  }
  writeGridPng(argv[1], grid, atlas, crt);
}
