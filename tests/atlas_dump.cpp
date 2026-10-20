// Renders the font lab screen to a PNG without opening a window, so glyph quality can be inspected
// (and diffed) at an exact cell size.
//   atlas_dump <out.png> [cellHeight=16] [fontIndex=0] [page=0] [mode=0] [invert=1] [crt=2]
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "png_grid.hpp"

#include "core/DataFile.hpp"
#include "render/GlyphAtlas.hpp"
#include "ui/FontShowcase.hpp"

using namespace laby;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: atlas_dump <out.png> [cellH] [fontIndex] [page] [mode] [invert] [crt]\n");
    return 2;
  }
  const int cellH = (argc > 2 ? std::atoi(argv[2]) : 16) & ~1, cellW = cellH / 2;
  const int fontIndex = argc > 3 ? std::atoi(argv[3]) : 0;
  ShowcaseState st;
  st.page = argc > 4 ? std::atoi(argv[4]) : 0;
  st.mode = static_cast<MosaicMode>(argc > 5 ? std::atoi(argv[5]) : 0);
  st.invert = argc > 6 ? std::atoi(argv[6]) != 0 : true;
  loadShowcaseArt(st);

  const auto fonts = findFonts(dataDir() + "/fonts");
  FontFace face;
  FontInfo fi;
  fi.fontCount = static_cast<int>(fonts.size()) + 1;
  fi.fontIndex = fontIndex;
  fi.cellW = cellW;
  fi.cellH = cellH;
  fi.name = "(none)";
  fi.crtLevel = argc > 7 ? std::atoi(argv[7]) : 2;
  if (fontIndex < static_cast<int>(fonts.size()) && face.load(fonts[static_cast<size_t>(fontIndex)].path)) fi.name = face.name();

  std::vector<FontFace> others(fonts.size());
  std::vector<const FontFace*> chain;
  if (face.loaded()) chain.push_back(&face);
  for (size_t i = 0; i < fonts.size(); ++i)
    if (others[i].load(fonts[i].path) && (!face.loaded() || others[i].path() != face.path())) chain.push_back(&others[i]);
  const Atlas atlas = buildGlyphAtlas(cellW, cellH, chain);
  Theme theme = loadTheme();
  CellGrid grid(kShowcaseW, kShowcaseH);
  drawShowcase(grid, theme, fi, st);
  const int crt = argc > 7 ? std::atoi(argv[7]) : 2;
  writeGridPng(argv[1], grid, atlas, crt);
  const int W = kShowcaseW * cellW, H = kShowcaseH * cellH;
  std::printf("%s: %dx%d px, cell %dx%d, font '%s', page %d mode %d\n", argv[1], W, H, cellW, cellH, fi.name.c_str(), st.page, static_cast<int>(st.mode));
}
