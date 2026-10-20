#include "ui/PanelKit.hpp"

namespace laby {

std::vector<std::string> wrap(const std::string& s, int width) {
  std::vector<std::string> out;
  std::string cur, word;
  const auto flush = [&] {
    if (cur.empty()) cur = word;
    else if (static_cast<int>(cur.size() + 1 + word.size()) <= width) cur += " " + word;
    else { out.push_back(cur); cur = word; }
    word.clear();
  };
  for (char c : s) {
    if (c == ' ') { if (!word.empty()) flush(); }
    else word += c;
  }
  if (!word.empty()) flush();
  if (!cur.empty()) out.push_back(cur);
  return out;
}

// The colour of an overlay line's tone.
Rgb toneColor(const Ctx& c, int t) {
  switch (t) {
    case 1: return c.hi;
    case 2: return c.dim;
    case 3: return c.th.better;  // better / worse: the theme's good / danger colors when they stand clear of the plain text, else a vivid green / red
    case 4: return c.th.worse;
    case 5: return c.th.coin;
    default: return c.fg;
  }
}

// A list row's highlight, in the action buttons' look: solid in the HP bar's colour, ending in half blocks so the fill stops where an
// outline would run. Two cell rows tall.
void highlightRow(Ctx& c, int x, int y, int w) {
  const Rgb f = c.th.selectFill;
  c.g.fill(x + 1, y, w - 2, 2, U' ', c.fg, f);
  for (int j = 0; j < 2; ++j) {
    c.g.put(x, y + j, U'\u2590', f, c.bg);
    c.g.put(x + w - 1, y + j, U'\u258C', f, c.bg);
  }
}

// Lines of facts (a name, then "label ... value" rows with dotted leaders), two cell rows each, from (x, y) down to `yEnd`.
void drawFacts(Ctx& c, int x, int y, int w, int yEnd, const std::vector<OverlayLine>& lines) {
  const int chars = c.chars(w);
  for (const OverlayLine& l : lines) {
    if (y + 2 > yEnd) break;
    const Rgb color = toneColor(c, l.tone);
    c.text(x, y, l.left.substr(0, static_cast<size_t>(chars)), color);
    if (!l.right.empty()) {
      const Rgb rcolor = l.rightTone >= 0 ? toneColor(c, l.rightTone) : color;
      const int lw = static_cast<int>(l.left.size()) * 11 / 6, rw = static_cast<int>(l.right.size()) * 11 / 6;
      c.textR(x, y, w, l.right, rcolor);
      for (int k = x + lw + 1; k < x + w - rw - 1; ++k) c.g.put(k, y + 1, U'·', mix(c.bg, c.dim, 0.5), c.bg);
    }
    y += 2;
  }
}

// A sub-menu.

}  // namespace laby
