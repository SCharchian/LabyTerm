#pragma once
#include <vector>

#include "platform/Theme.hpp"
#include "render/Backend.hpp"
#include "render/CellGrid.hpp"
#include "render/Mosaic.hpp"

namespace laby {

constexpr int kShowcaseW = 130, kShowcaseH = 52;

struct ShowcaseState {
  MosaicMode mode = MosaicMode::Octant2x4;
  int page = 0;
  bool invert = true;  // light pixels are ink: solid blocks bright, mortar/cracks dark
  std::vector<Bitmap1> art;
};

int showcasePageCount();
void loadShowcaseArt(ShowcaseState& st);  // (re)loads the art for st.page from the original Labyrinth images
// Draws the whole lab screen into `grid` (kShowcaseW x kShowcaseH).
void drawShowcase(CellGrid& grid, const Theme& theme, const FontInfo& font, const ShowcaseState& st);

// `labyterm --fonts`: the font & glyph lab. Shows text in the available fonts, the synthesized glyph
// families, and the original Labyrinth art converted to text in several styles.
//   F  next font     M  mosaic style     A  art page     I  invert ink     +/-  cell size     0  auto-fit     K  CRT filter level
int runFontShowcase();

}  // namespace laby
