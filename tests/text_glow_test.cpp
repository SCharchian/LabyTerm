// The Text dungeon style's glow rule: a cell that glows is inked in an object's own colour, never in a wall, shade or floor colour.
// (Where an object's edge shares a cell with the wall behind it, the cell takes one colour; the glow must follow that colour.)
//   text_glow_test
#include <cstdio>
#include <vector>

#include "core/Settings.hpp"
#include "platform/Theme.hpp"
#include "render/Mosaic.hpp"
#include "ui/ScreenLayout.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;

static int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);                \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

// The cell logic on its own: a wall everywhere, an object over parts of it.
static void cellLogic() {
  const Rgb paper{10, 10, 10}, wallInk{80, 120, 80}, objInk{240, 200, 90};
  const int cols = 5, w = cols * 4, h = 8;  // density 2: 4x8 pixels a cell, one row of cells
  Bitmap1 walls(w, h), objects(w, h);
  std::fill(walls.px.begin(), walls.px.end(), Bitmap1::Light);
  const auto lit = [&](int cell, int count) {  // `count` object pixels in the cell (the first ones in reading order)
    for (int k = 0; k < count; ++k) objects.px[static_cast<size_t>((k / 4) * w + cell * 4 + k % 4)] = Bitmap1::Light;
  };
  lit(1, 32);  // all of it
  lit(2, 3);   // a few: an edge the wall dominates
  lit(3, 14);  // nearly half: an edge the object dominates
  CellGrid g(cols, 1);
  drawMosaicStack(g, 0, 0, {{&objects, objInk, true}, {&walls, wallInk}}, paper, true, 2);
  CHECK(g.at(0, 0).fg == wallInk && !g.at(0, 0).glow);      // plain wall
  CHECK(g.at(1, 0).fg == objInk && g.at(1, 0).glow == 2);   // object
  CHECK(g.at(2, 0).fg == wallInk && !g.at(2, 0).glow);      // a few object pixels: the wall's colour, so no glow either
  CHECK(g.at(3, 0).fg == objInk && g.at(3, 0).glow == 2);   // mostly object pixels: the object's colour and its glow
  CHECK(g.at(4, 0).fg == wallInk && !g.at(4, 0).glow);
}

// Counts the glowing cells of the current view, and those in a colour that is not one of the theme's object colours.
static void tally(WalkSession& s, const Theme& th, int& glowCells, int& bad) {
  const Rgb allowed[] = {th.accent, th.coin, th.lift, th.mana, th.lair};
  CellGrid grid(kGridCols, kGridRows);
  s.draw(grid, th, "test");
  for (int y = kViewportY; y < kViewportY + kViewportRows; ++y)
    for (int x = kViewportX; x < kViewportX + kViewportCols; ++x) {
      const Cell& c = grid.at(x, y);
      if (!c.glow) continue;
      ++glowCells;
      bool ok = false;
      for (const Rgb& a : allowed) ok |= c.fg == a;
      bad += !ok;
    }
}

// Real views of every kind of object, from 25 floors each.
static void scenes(const Theme& th) {
  settings().mazeStyle = 1;
  struct Result { const char* name; int glowCells = 0, bad = 0; };
  Result chest{"chest"}, lair{"lair"}, lift{"elevator"}, fountain{"fountain"};
  for (std::uint64_t seed = 1; seed <= 25; ++seed) {
    {
      WalkSession s(seed);
      if (s.debugGoNextTo(TileClass::TREASURE)) tally(s, th, chest.glowCells, chest.bad);
    }
    {
      WalkSession s(seed);
      if (s.debugGoNextTo(TileClass::BOSS)) tally(s, th, lair.glowCells, lair.bad);
    }
    {  // the elevator is where you start: walk off a few steps and look all round, back at it included
      WalkSession s(seed);
      for (int step = 0; step < 3; ++step, s.handle(Key::Up))
        for (int turn = 0; turn < 4; ++turn, s.handle(Key::Right)) tally(s, th, lift.glowCells, lift.bad);
    }
    {  // the fountain shows once the secret door has slid up (before that its slab, after that the offer's panel covers the view)
      WalkSession s(seed);
      s.tick(0);
      if (s.debugGoNextTo(TileClass::SECRET)) {
        s.handle(Key::Up);
        for (int i = 1; i <= 16; ++i) {
          s.tick(1000 + static_cast<std::uint64_t>(i) * 100);
          if (i >= 12) tally(s, th, fountain.glowCells, fountain.bad);
        }
      }
    }
  }
  for (const Result& r : {chest, lair, lift, fountain}) {
    std::printf("%-9s glowing cells %5d, in a non-object colour %d\n", r.name, r.glowCells, r.bad);
    CHECK(r.glowCells > 0);
    CHECK(r.bad == 0);
  }
}

int main() {
  cellLogic();
  scenes(loadTheme());
  return failures ? 1 : 0;
}
