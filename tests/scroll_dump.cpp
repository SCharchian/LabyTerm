// Renders the scroll picture options side by side: scroll_dump <out.png> [t=3] [cols=44] [rows=22] [cellH=12]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "png_grid.hpp"

#include "core/DataFile.hpp"
#include "platform/Theme.hpp"
#include "render/GlyphAtlas.hpp"
#include "ui/ScrollArt.hpp"

using namespace laby;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: scroll_dump <out.png> [t] [cols] [rows] [cellH]\n");
    return 2;
  }
  const double t = argc > 2 ? std::atof(argv[2]) : 3.0;
  const int cols = argc > 3 ? std::atoi(argv[3]) : 44, rows = argc > 4 ? std::atoi(argv[4]) : 22;
  const int cellH = (argc > 5 ? std::atoi(argv[5]) : 12) & ~1;
  const Theme th = loadTheme();
  const auto fonts = findFonts(dataDir() + "/fonts");
  std::vector<FontFace> faces(fonts.size());
  std::vector<const FontFace*> chain;
  for (size_t i = 0; i < fonts.size(); ++i)
    if (faces[i].load(fonts[i].path)) chain.push_back(&faces[i]);
  const Atlas atlas = buildGlyphAtlas(cellH / 2, cellH, chain, 11);
  const int gap = 2;
  CellGrid grid(kScrollVariants * (cols + gap) + gap, rows + 4);
  grid.clear(th.bg);
  const ScrollColors pal = scrollColors(th);
  for (int v = 0; v < kScrollVariants; ++v) {
    const int x0 = gap + v * (cols + gap);
    drawScroll(grid, x0, 3, cols, rows, v, pal, t);
    const std::string label = std::to_string(v + 1);
    grid.put(x0, 1, static_cast<char32_t>(U'1' + v), th.accent, th.bg);
  }
  writeGridPng(argv[1], grid, atlas, 0);
  std::printf("%s\n", argv[1]);
}
