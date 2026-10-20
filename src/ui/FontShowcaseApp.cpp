#include <algorithm>

#include "ui/FontShowcase.hpp"
#include "ui/StatusTag.hpp"

namespace laby {

int runFontShowcase() {
  Theme theme = loadTheme();
  ThemeWatcher themeWatcher;
  auto backend = makeBackend(kShowcaseW, kShowcaseH, "LabyTerm: font and glyph lab");
  CellGrid grid(kShowcaseW, kShowcaseH);
  ShowcaseState st;
  loadShowcaseArt(st);
  int forced = 0;
  Toast toast;

  bool dirty = true;
  bool toastShown = false;
  for (bool running = true; running;) {
    Event e;
    // Sleep until input, a window event, or a timer (1 s theme/tuning check, toast expiry): an idle game uses ~0% CPU
    const std::uint64_t now0 = backend->ticksMs();
    const int wakeMs = toast.active(now0) ? std::min(1000, toast.msLeft(now0) + 5) : 1000;
    for (bool have = backend->waitEvent(e, wakeMs); have; have = backend->pollEvent(e)) {
      dirty = true;
      if (e.type == Event::Type::Quit) running = false;
      if (e.type != Event::Type::KeyDown) continue;
      if (e.key == Key::Escape) running = false;
      switch (e.text) {
        case 'f': {
          backend->nextFont();
          const FontInfo fi = backend->fontInfo();
          toast.show("Font " + std::to_string(fi.fontIndex + 1) + "/" + std::to_string(fi.fontCount) + ": " + fi.name, backend->ticksMs());
          break;
        }
        case 'k':
          backend->setCrtLevel((backend->crtLevel() + 1) % 4);
          toast.show("CRT level " + std::to_string(backend->crtLevel()), backend->ticksMs());
          break;
        case 'm': st.mode = nextMosaicMode(st.mode); break;
        case 'a': st.page = (st.page + 1) % showcasePageCount(); loadShowcaseArt(st); break;
        case 'i': st.invert = !st.invert; break;
        case '=': case '+': forced = (forced == 0 ? backend->fontInfo().cellH : forced) + 2; backend->setCellHeight(forced); break;
        case '-': forced = std::max(8, (forced == 0 ? backend->fontInfo().cellH : forced) - 2); backend->setCellHeight(forced); break;
        case '0': forced = 0; backend->setCellHeight(0); break;
        default: break;
      }
    }
    if (themeWatcher.poll(theme, backend->ticksMs())) dirty = true;
    const bool toastNow = toast.active(backend->ticksMs());
    if (toastNow != toastShown) {
      toastShown = toastNow;
      dirty = true;  // the banner just appeared or expired
    }
    if (!dirty && !backend->needsFrame()) continue;
    dirty = false;

    drawShowcase(grid, theme, backend->fontInfo(), st);
    toast.draw(grid, theme, backend->ticksMs(), backend->textSixths());
    backend->present(grid);
  }
  return 0;
}

}  // namespace laby
