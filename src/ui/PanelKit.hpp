#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "platform/Theme.hpp"
#include "render/CellGrid.hpp"
#include "ui/MainScreen.hpp"

namespace laby {

// Building blocks for the main screen's panels (outlines, text, bars), shared by the game and the layout mocks.
// Cell units unless noted; overlay text is 11/6 of a cell wide and two cell rows tall.

// Glyphs for the panel outlines. Thin is the font's own hairline; heavy and double are thicker.
struct Lines {
  char32_t h, v, tl, tr, bl, br, lt, rt, tt, bt, x;
};
inline constexpr Lines kLineStyles[3] = {
    {U'\u2500', U'\u2502', U'\u250C', U'\u2510', U'\u2514', U'\u2518', U'\u251C', U'\u2524', U'\u252C', U'\u2534', U'\u253C'},
    {U'\u2501', U'\u2503', U'\u250F', U'\u2513', U'\u2517', U'\u251B', U'\u2523', U'\u252B', U'\u2533', U'\u253B', U'\u254B'},
    {U'\u2550', U'\u2551', U'\u2554', U'\u2557', U'\u255A', U'\u255D', U'\u2560', U'\u2563', U'\u2566', U'\u2569', U'\u256C'},
};

struct Ctx {
  CellGrid& g;
  const Theme& th;
  Rgb fg, dim, hi, bg, line, panel;
  Lines ln = kLineStyles[0];
  int chars(int cols) const { return std::max(0, cols * 6 / 11); }  // overlay text is 11/6 of a cell wide
  // One line of overlay text with its top-left at cell (x, y); it covers two cell rows.
  void text(int x, int y, const std::string& t, Rgb col) const { g.printText(x, y, 0, 0, t, col); }
  void textR(int x, int y, int w, const std::string& t, Rgb col) const {  // right-aligned in `w` cells
    const int chars_ = static_cast<int>(t.size());
    g.printText(x + std::max(0, w - chars_ * 11 / 6), y, 0, 0, t, col);
  }
  void bar(int x, int y, int w, double frac, Rgb fill, Rgb empty) const {
    static const char32_t kPart[] = {U' ', U'▏', U'▎', U'▍', U'▌', U'▋', U'▊', U'▉'};
    const double filled = std::clamp(frac, 0.0, 1.0) * w;
    const int whole = static_cast<int>(filled), eighths = static_cast<int>((filled - whole) * 8);
    for (int i = 0; i < w; ++i) {
      if (i < whole) g.put(x + i, y, U'█', fill, bg);
      else if (i == whole && eighths > 0) g.put(x + i, y, kPart[eighths], fill, empty);
      else g.put(x + i, y, U' ', fill, empty);
    }
  }
  void clear(int x, int y, int w, int h) const {
    g.eraseText(x, y, w, h, 11.0 / 6.0);
    g.addTorchHole(x, y, w, h);
    g.fill(x, y, w, h, U' ', fg, bg);
  }
  void rect(int x, int y, int w, int h, Rgb col) const { rect(x, y, w, h, col, bg); }
  void rect(int x, int y, int w, int h, Rgb col, Rgb cellBg) const {
    for (int i = 1; i < w - 1; ++i) {
      g.put(x + i, y, ln.h, col, cellBg);
      g.put(x + i, y + h - 1, ln.h, col, cellBg);
    }
    for (int j = 1; j < h - 1; ++j) {
      g.put(x, y + j, ln.v, col, cellBg);
      g.put(x + w - 1, y + j, ln.v, col, cellBg);
    }
    g.put(x, y, ln.tl, col, cellBg);
    g.put(x + w - 1, y, ln.tr, col, cellBg);
    g.put(x, y + h - 1, ln.bl, col, cellBg);
    g.put(x + w - 1, y + h - 1, ln.br, col, cellBg);
  }
  void frame(int x, int y, int w, int h, const std::string& title, Rgb col) const {
    rect(x, y, w, h, col);
    if (!title.empty()) g.print(x + 2, y, "[ " + title + " ]", hi, bg);
  }
  // A horizontal rule from the left border to the right border (x0, x1 inclusive), with a title in it.
  void hrule(int x0, int x1, int y, const std::string& title = "") const {
    for (int x = x0 + 1; x < x1; ++x) g.put(x, y, ln.h, line, bg);
    g.put(x0, y, ln.lt, line, bg);
    g.put(x1, y, ln.rt, line, bg);
    if (!title.empty()) g.print(x0 + 2, y, "[ " + title + " ]", hi, bg);
  }
  // A vertical rule that runs between two horizontal ones.
  void vrule(int x, int y0, int y1) const {
    for (int y = y0 + 1; y < y1; ++y) g.put(x, y, ln.v, line, bg);
    g.put(x, y0, ln.tt, line, bg);
    g.put(x, y1, ln.bt, line, bg);
  }
  void hint(int rightEdge, int y, const std::string& t) const { g.print(rightEdge - static_cast<int>(t.size()), y, t, dim, bg); }
};

// The colour of an overlay line's tone (see OverlayLine).
Rgb toneColor(const Ctx& c, int tone);
// A list row's highlight, in the action buttons' look: solid in the HP bar's colour, ending in half blocks. Two cell rows tall.
void highlightRow(Ctx& c, int x, int y, int w);
// Lines of facts (a name, then "label ... value" rows with dotted leaders), two cell rows each, from (x, y) down to `yEnd`.
void drawFacts(Ctx& c, int x, int y, int w, int yEnd, const std::vector<OverlayLine>& lines);

// Word-wraps `s` to lines of at most `width` characters.
std::vector<std::string> wrap(const std::string& s, int width);

}  // namespace laby
