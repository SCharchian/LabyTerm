// Times what the CPU does to put the dungeon on screen, per maze style (a repeatable stand-in for playing and watching top).
//   draw_bench [cellH=24] [reps=100]
// For each style: (1) a redraw with nothing changed (what the torch's breathing does ~4x a second: the traced view is cached, the
// cell grid is rebuilt), (2) a step or turn (the view is traced again, then drawn), and the base-frame render of the finished
// grid (renderGrid, the CPU part of presenting; the CRT filter runs on the GPU in the game).
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "core/DataFile.hpp"
#include "core/Settings.hpp"
#include "platform/Theme.hpp"
#include "render/FrameRenderer.hpp"
#include "render/GlyphAtlas.hpp"
#include "ui/ScreenLayout.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

int main(int argc, char** argv) {
  const int cellH = (argc > 1 ? std::atoi(argv[1]) : 24) & ~1, reps = argc > 2 ? std::atoi(argv[2]) : 100;
  const auto fonts = findFonts(dataDir() + "/fonts");
  std::vector<FontFace> faces(fonts.size());
  std::vector<const FontFace*> chain;
  for (size_t i = 0; i < fonts.size(); ++i)
    if (faces[i].load(fonts[i].path)) chain.push_back(&faces[i]);
  const Atlas atlas = buildGlyphAtlas(cellH / 2, cellH, chain, 11);
  const Theme theme = loadTheme();
  std::printf("cell %dx%d, %d reps, ms per call\n", cellH / 2, cellH, reps);
  for (int style = 1; style >= 0; --style) {
    settings().mazeStyle = style;
    WalkSession session(7);
    session.handle(Key::Up);
    CellGrid grid(kGridCols, kGridRows);
    session.draw(grid, theme, "bench");  // warm up: traces the view, builds the glyph shapes
    auto t0 = Clock::now();
    for (int i = 0; i < reps; ++i) session.draw(grid, theme, "bench");
    const double redraw = ms(t0, Clock::now()) / reps;
    t0 = Clock::now();
    for (int i = 0; i < reps; ++i) {
      session.handle(i % 2 ? Key::Left : Key::Right);  // a turn: the camera changes, so the view is traced again
      session.draw(grid, theme, "bench");
    }
    const double turn = ms(t0, Clock::now()) / reps;
    Frame frame;
    renderGrid(grid, atlas, frame);
    t0 = Clock::now();
    for (int i = 0; i < reps; ++i) renderGrid(grid, atlas, frame);
    const double base = ms(t0, Clock::now()) / reps;
    std::printf("%-7s redraw (cached view) %6.2f   turn (trace + draw) %7.2f   renderGrid %6.2f\n", style ? "text" : "pixel art", redraw, turn, base);
  }
}
