#include "ui/FontShowcase.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#include "render/GlyphAtlas.hpp"
#include "ui/ViewPainter.hpp"
#include "ui/StatusTag.hpp"

namespace laby {
namespace {

struct ArtPage {
  const char* title;
  bool invert;
  std::vector<const char*> files;
};

const std::vector<ArtPage>& pages() {
  static const std::vector<ArtPage> p = {
      {"walls and doors, depth 1", false, {"1FrontWall_sprite", "1FrontDoorClosed_sprite"}},
      {"monsters", false, {"Monsters/Animal/Dragon", "Monsters/Human/Ninja", "Monsters/Undead/Vampire"}},
      {"wall pieces at depths 0-3", false, {"0LeftWall_sprite", "1LeftWall_sprite", "2FrontWall_sprite", "3FrontWall_sprite", "1RightWall_sprite", "0RightWall_sprite"}},
      {"darkness vignette (dithered overlay)", true, {"Dungeon_Darkness-table-1", "DialogBoxFrame"}},
  };
  return p;
}



}  // namespace

int showcasePageCount() { return static_cast<int>(pages().size()); }

void loadShowcaseArt(ShowcaseState& st) {
  st.art.clear();
  const ArtPage& p = pages()[static_cast<size_t>(st.page)];
  for (const char* f : p.files) st.art.push_back(loadOriginalArt(f));
}

void drawShowcase(CellGrid& grid, const Theme& theme, const FontInfo& font, const ShowcaseState& st) {
    grid.clear(theme.bg);
    const FontInfo& fi = font;
    const Rgb fg = theme.fg, dim = theme.muted, hi = theme.accent;
    int y = 0;
    grid.print(1, y++, "LABYTERM  FONT AND GLYPH LAB", hi, theme.bg);
    grid.print(1, y++, settingsText(fi) + "   grid " + std::to_string(kShowcaseW) + "x" + std::to_string(kShowcaseH) + (fi.autoFit ? " (auto-fit)" : " (forced cell)"), fg, theme.bg);
    grid.print(1, y++, "[F] font  [M] pixel-art style  [A] art page  [I] invert  [+/-] cell size  [0] auto-fit  [K] CRT filter  [Esc] quit", dim, theme.bg);
    y++;

    grid.print(1, y++, "TEXT (rasterized from the font)", hi, theme.bg);
    grid.print(1, y++, "The quick brown fox jumps over the lazy dog 0123456789", fg, theme.bg);
    grid.print(1, y++, "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~   \xC3\x80\xC3\x81\xC3\x82\xC3\x83\xC3\x84\xC3\x85\xC3\x86\xC3\x87\xC3\x88\xC3\x89 \xC3\xB1 \xC3\xB6 \xC3\xBC \xC3\x9F \xC2\xBF \xC2\xA1 \xC2\xAB \xC2\xBB \xE2\x82\xAC \xC2\xA3", fg, theme.bg);
    grid.print(1, y++, "\xE2\x97\x8F \xE2\x97\x8B \xE2\x96\xA0 \xE2\x96\xA1 \xE2\x96\xB2 \xE2\x96\xBC \xE2\x97\x80 \xE2\x96\xB6 \xE2\x98\x85 \xE2\x99\xA5 \xE2\x99\xA6 \xE2\x99\xA3 \xE2\x99\xA0 \xE2\x9C\x93 \xE2\x9C\x97 \xE2\x92\xB6 \xE2\x92\xB7 \xE2\x86\x90 \xE2\x86\x91 \xE2\x86\x92 \xE2\x86\x93 \xE2\x80\xA6 \xE2\x80\xA2 \xC2\xB0 \xC2\xB1 \xC3\x97 \xC3\xB7 \xE2\x89\xA4 \xE2\x89\xA5", fg, theme.bg);
    grid.print(1, y++, "Level 7   HP 42/50   MP 11/20   Gold 1,234   Experience 380/412", hi, theme.bg);
    y++;

    grid.print(1, y++, "SYNTHESIZED GLYPHS (pixel-exact at every cell size, no font needed)", hi, theme.bg);
    grid.print(1, y++, "blocks  \xE2\x96\x80\xE2\x96\x81\xE2\x96\x82\xE2\x96\x83\xE2\x96\x84\xE2\x96\x85\xE2\x96\x86\xE2\x96\x87\xE2\x96\x88\xE2\x96\x89\xE2\x96\x8A\xE2\x96\x8B\xE2\x96\x8C\xE2\x96\x8D\xE2\x96\x8E\xE2\x96\x8F\xE2\x96\x90 \xE2\x96\x91\xE2\x96\x92\xE2\x96\x93 \xE2\x96\x94\xE2\x96\x95 \xE2\x96\x96\xE2\x96\x97\xE2\x96\x98\xE2\x96\x99\xE2\x96\x9A\xE2\x96\x9B\xE2\x96\x9C\xE2\x96\x9D\xE2\x96\x9E\xE2\x96\x9F", theme.green, theme.bg);
    {
      const int y0 = y;
      grid.print(1, y0, "boxes", dim, theme.bg);
      grid.print(8, y0, "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\xAC\xE2\x94\x80\xE2\x94\x90  \xE2\x94\x8F\xE2\x94\x81\xE2\x94\xB3\xE2\x94\x81\xE2\x94\x93  \xE2\x95\x94\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x97  \xE2\x95\xB1\xE2\x95\xB2\xE2\x95\xB3", theme.cyan, theme.bg);
      grid.print(8, y0 + 1, "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\xBC\xE2\x94\x80\xE2\x94\xA4  \xE2\x94\xA3\xE2\x94\x81\xE2\x95\x8B\xE2\x94\x81\xE2\x94\xAB  \xE2\x95\x91   \xE2\x95\x91  \xE2\x95\xB3\xE2\x95\xB1\xE2\x95\xB2", theme.cyan, theme.bg);
      grid.print(8, y0 + 2, "\xE2\x94\x94\xE2\x94\x80\xE2\x94\xB4\xE2\x94\x80\xE2\x94\x98  \xE2\x94\x97\xE2\x94\x81\xE2\x94\xBB\xE2\x94\x81\xE2\x94\x9B  \xE2\x95\x9A\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x9D  \xE2\x95\xB2\xE2\x95\xB3\xE2\x95\xB1", theme.cyan, theme.bg);
      y += 3;
    }
    grid.print(1, y, "braille", dim, theme.bg);
    grid.print(9, y++, "\xE2\xA0\x81\xE2\xA0\x83\xE2\xA0\x87\xE2\xA1\x87\xE2\xA3\x87\xE2\xA3\xA7\xE2\xA3\xB7\xE2\xA3\xBF \xE2\xA1\x80\xE2\xA1\x84\xE2\xA1\x86\xE2\xA1\x87 \xE2\xA0\x89\xE2\xA0\x9B\xE2\xA0\xBF", theme.yellow, theme.bg);
    grid.print(1, y, "sextants", dim, theme.bg);
    for (unsigned p = 1; p <= 40; ++p) grid.put(10 + static_cast<int>(p), y, sextantGlyph(p), theme.magenta, theme.bg);
    y++;
    grid.print(1, y, "pixel art", dim, theme.bg);
    for (unsigned p = 0; p < 64; ++p) grid.put(10 + static_cast<int>(p), y, mosaicGlyph(p * 4 + (p % 4)), theme.red, theme.bg);
    y += 2;

    grid.print(1, y++, std::string("ORIGINAL ART AS TEXT: ") + pages()[static_cast<size_t>(st.page)].title + "   style: " + mosaicName(st.mode), hi, theme.bg);
    int x = 1;
    int rowMax = 0;
    std::string sizes;
    for (const Bitmap1& b : st.art) {
      if (b.empty()) {
        grid.print(x, y, "(art not found)", theme.red, theme.bg);
        continue;
      }
      const CellSize sz = mosaicSize(b, st.mode);
      if (x + sz.cols > kShowcaseW - 1 && x > 1) { x = 1; y += rowMax + 1; rowMax = 0; }
      drawMosaic(grid, x, y, b, st.mode, theme.accent, theme.bg, pages()[static_cast<size_t>(st.page)].invert != st.invert);
      sizes += std::to_string(b.w) + "x" + std::to_string(b.h) + "px = " + std::to_string(sz.cols) + "x" + std::to_string(sz.rows) + " cells   ";
      x += sz.cols + 3;
      rowMax = std::max(rowMax, sz.rows);
    }
    grid.print(1, kShowcaseH - 1, sizes, dim, theme.bg);
}

}  // namespace laby
