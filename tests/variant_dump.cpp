// Renders one main-screen layout variant to a PNG.
//   variant_dump <out.png> <variant 1-6> [combat|corridor] [cellH=12] [crt=2] [action=0] [scroll=1]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "png_grid.hpp"

#include "core/DataFile.hpp"
#include "render/GlyphAtlas.hpp"
#include "ui/LayoutVariants.hpp"

using namespace laby;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: variant_dump <out.png> <variant 1-6> [combat][+scrolls] [cellH] [crt] [action] [scroll] [lineStyle 0-2, default 2] [notheme]\n");
    return 2;
  }
  const int variant = std::atoi(argv[2]);
  const std::string flags = argc > 3 ? argv[3] : "";
  const bool combat = flags.find("combat") != std::string::npos, scrolls = flags.find("scrolls") != std::string::npos;
  const int cellH = (argc > 4 ? std::atoi(argv[4]) : 12) & ~1, cellW = cellH / 2;
  const int crt = argc > 5 ? std::atoi(argv[5]) : 2;
  const int sel = argc > 6 ? std::atoi(argv[6]) : 0, scroll = argc > 7 ? std::atoi(argv[7]) : 2, lineStyle = argc > 8 ? std::atoi(argv[8]) : 2;
  const bool useTheme = !(argc > 9 && std::string(argv[9]) == "notheme");
  const auto fonts = findFonts(dataDir() + "/fonts");
  std::vector<FontFace> faces(fonts.size());
  std::vector<const FontFace*> chain;
  for (size_t i = 0; i < fonts.size(); ++i)
    if (faces[i].load(fonts[i].path)) chain.push_back(&faces[i]);
  const Atlas atlas = buildGlyphAtlas(cellW, cellH, chain, 11);
  int cols = 0, rows = 0;
  layoutVariantGrid(variant, cols, rows);
  CellGrid grid(cols, rows);
  drawLayoutVariant(grid, loadTheme(useTheme), variant, combat, sel, scroll, scrolls, lineStyle);
  writeGridPng(argv[1], grid, atlas, crt);
  std::printf("%s: variant %d, grid %dx%d cells = %dx%d px\n", argv[1], variant, cols, rows, cols * cellW, rows * cellH);
}
