#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

#include "platform/Theme.hpp"
#include "render/Backend.hpp"
#include "render/CellGrid.hpp"

namespace laby {

// The current font / CRT level / cell size as one line, e.g. "font 1/5 VT323-Regular.ttf | CRT 2 | cell 6x12 | text 2x".
inline std::string textSizeName(int sixths) {
  switch (sixths) {
    case 6: return "1x";
    case 9: return "1.5x";
    case 11: return "1.83x";
    case 12: return "2x";
    default: return std::to_string(sixths / 6.0).substr(0, 4) + "x";
  }
}

inline std::string settingsText(const FontInfo& f, bool withText = true) {
  std::string s = "font " + std::to_string(f.fontIndex + 1) + "/" + std::to_string(f.fontCount) + " " + f.name + " | CRT " +
                  std::to_string(f.crtLevel) + " | cell " + std::to_string(f.cellW) + "x" + std::to_string(f.cellH);
  if (withText) s += " | text " + textSizeName(f.textSixths);
  return s;
}

// Shown in the bottom border of the developer views so you always know which setting you are looking at.
inline void drawStatusTag(CellGrid& g, const Theme& th, const std::string& text) {
  const std::string tag = "[ " + text + " ]";
  const int x = g.width() - 2 - static_cast<int>(tag.size());
  g.print(std::max(1, x), g.height() - 1, tag, th.yellow, th.bg);
}

// A big, centered, framed message that disappears after a couple of seconds: shown when you change a
// setting (font, CRT level, text size) so you can read exactly which one you are looking at.
struct Toast {
  std::string text;
  std::uint64_t until = 0;

  void show(std::string t, std::uint64_t nowMs, unsigned durationMs = 2500) {
    text = std::move(t);
    until = nowMs + durationMs;
  }
  bool active(std::uint64_t nowMs) const { return !text.empty() && nowMs < until; }
  // Milliseconds until it disappears (0 if not showing)
  int msLeft(std::uint64_t nowMs) const { return active(nowMs) ? static_cast<int>(until - nowMs) : 0; }

  // `textSixths` is the current text size (so the banner box can be sized to the text)
  void draw(CellGrid& g, const Theme& th, std::uint64_t nowMs, int textSixths) const {
    if (text.empty() || nowMs >= until) return;
    const int n = std::max(6, textSixths);
    const int chars = std::min(static_cast<int>(text.size()), (g.width() - 8) * 6 / n);
    const int w = (chars * n + 5) / 6 + 4, h = (n + 5) / 6 + 4;  // text width / height in cells, plus margins
    const int x = (g.width() - w) / 2, y = std::max(0, g.height() / 2 - h / 2);
    g.fill(x, y, w, h, U' ', th.fg, th.bg);
    g.box(x, y, w, h, th.yellow, th.bg);
    g.printText(x + 2, y + 2, 0, 0, text.substr(0, static_cast<size_t>(chars)), th.accent);
  }
};

}  // namespace laby
