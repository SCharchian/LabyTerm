#include <algorithm>

#include "core/Tuning.hpp"
#include "render/Backend.hpp"
#include "ui/LayoutVariants.hpp"
#include "ui/StatusTag.hpp"

namespace laby {

int runLayoutVariants(int variant, bool combat) {
  variant = std::clamp(variant, 1, kLayoutVariantCount);
  int cols = 0, rows = 0;
  layoutVariantGrid(variant, cols, rows);
  Theme theme = loadTheme();
  ThemeWatcher themeWatcher;
  auto backend = makeBackend(cols, rows, "LabyTerm: layouts");
  CellGrid grid(cols, rows);
  int sel = 0, scroll = 2, lineStyle = 2;  // double lines: the author's choice
  bool useTheme = tuning().display.themeColors != 0;
  bool scrolls = false;
  const int ac = layoutVariantActionCols(variant);
  bool dirty = true;
  for (bool running = true; running;) {
    Event e;
    for (bool have = backend->waitEvent(e, 1000); have; have = backend->pollEvent(e)) {
      dirty = true;
      if (e.type == Event::Type::Quit) running = false;
      if (e.type != Event::Type::KeyDown) continue;
      if (e.key == Key::Escape) running = false;
      if (e.key == Key::Left) sel = (sel + 5) % 6;
      if (e.key == Key::Right) sel = (sel + 1) % 6;
      if (e.key == Key::Up) sel = (sel + 6 - ac) % 6;
      if (e.key == Key::Down) sel = (sel + ac) % 6;
      if (e.text == '[') scroll = (scroll + 7) % 8;
      if (e.text == ']') scroll = (scroll + 1) % 8;
      if (e.text == 'c') combat = !combat;
      if (e.text == 's') scrolls = !scrolls;
      if (e.text == 'l') lineStyle = (lineStyle + 1) % 3;
      if (e.text == 't') {
        useTheme = !useTheme;
        theme = loadTheme(useTheme);
      }
    }
    if (themeWatcher.poll(theme, backend->ticksMs())) {
      theme = loadTheme(useTheme);
      dirty = true;
    }
    if (!dirty && !backend->needsFrame()) continue;
    dirty = false;
    drawLayoutVariant(grid, theme, variant, combat, sel, scroll, scrolls, lineStyle);
    drawStatusTag(grid, theme,
                  "layout " + std::to_string(variant) + " | " + std::to_string(cols) + "x" + std::to_string(rows) + " | " +
                      settingsText(backend->fontInfo(), false) + " | theme colors " + (theme.fromOmarchy ? "on" : "off") +
                      " (T) | C combat, S scrolls, L lines");
    backend->present(grid);
  }
  return 0;
}

}  // namespace laby
