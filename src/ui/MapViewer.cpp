#include <algorithm>
#include "ui/MapViewer.hpp"

#include <random>
#include <string>

#include "core/Rng.hpp"
#include "core/Tuning.hpp"
#include "game/Dungeon.hpp"
#include "platform/Theme.hpp"
#include "render/Backend.hpp"
#include "ui/StatusTag.hpp"

namespace laby {
namespace {

struct Look {
  char32_t glyph;
  Rgb fg;
};

Look lookFor(TileClass t, const Theme& th, bool showSecret) {
  switch (t) {
    case TileClass::WALL: return {U'█', th.selection};
    case TileClass::SOIL: return {U'▒', th.selection};
    case TileClass::VEIN: return {U'*', th.accent};
    case TileClass::FLOOR: return {U'·', th.muted};
    case TileClass::C_DOOR: return {U'+', th.yellow};
    case TileClass::O_DOOR: return {U'\'', th.yellow};
    case TileClass::ELEVATOR: return {U'>', th.cyan};
    case TileClass::BOSS: return {U'B', th.red};
    case TileClass::TREASURE: return {U'T', th.accent};
    case TileClass::SECRET: return showSecret ? Look{U'$', th.magenta} : Look{U'█', th.selection};
    case TileClass::EMPTY: return {U' ', th.bg};
    default: return {static_cast<char32_t>(t), th.fg};
  }
}

}  // namespace

int runMapViewer(std::uint64_t seed) {
  constexpr int kGridW = 64, kGridH = 36;
  Theme theme = loadTheme();
  ThemeWatcher themeWatcher;
  auto backend = makeBackend(kGridW, kGridH, "LabyTerm: dungeon generator");
  CellGrid grid(kGridW, kGridH);

  const Tuning::Dungeon t = tuning().dungeon;
  const auto notLooted = [](int) { return false; };
  Dungeon dungeon(t.sizeX, t.sizeY);
  int floorNo = 1;
  bool showSecret = false;
  std::random_device rd;

  auto regenerate = [&] {
    Rng rng(seed);
    LevelGenState gen;
    dungeon.beginFloor(floorNo, rng, gen, t, notLooted);
  };
  regenerate();

  bool dirty = true;
  for (bool running = true; running;) {
    Event e;
    // Sleep until input, a window event, or a timer (1 s theme/tuning check, toast expiry): an idle game uses ~0% CPU
    const int wakeMs = 1000;
    for (bool have = backend->waitEvent(e, wakeMs); have; have = backend->pollEvent(e)) {
      dirty = true;
      if (e.type == Event::Type::Quit) running = false;
      if (e.type != Event::Type::KeyDown) continue;
      if (e.text == 's') showSecret = !showSecret;
      switch (e.key) {
        case Key::Escape: running = false; break;
        case Key::Enter: case Key::A: seed = (static_cast<std::uint64_t>(rd()) << 8) ^ rd(); regenerate(); break;
        case Key::Up: ++floorNo; regenerate(); break;
        case Key::Down: if (floorNo > 1) --floorNo; regenerate(); break;
        default: break;
      }
    }
    if (themeWatcher.poll(theme, backend->ticksMs())) dirty = true;

    if (!dirty && !backend->needsFrame()) continue;
    dirty = false;

    grid.clear(theme.bg);
    const Level& lv = dungeon.level();
    // The Lua matrix has a border ring (rows 0 and height+1, cols 0 and width+1): draw rows/cols 0..size+1.
    for (int r = 0; r <= lv.height() + 1; ++r)
      for (int c = 0; c <= lv.width() + 1; ++c) {
        const Look look = lookFor(lv.tile(r, c).cls, theme, showSecret);
        grid.put(1 + c, 1 + r, look.glyph, look.fg, theme.bg);
      }
    grid.box(0, 0, lv.width() + 4, lv.height() + 4, theme.green, theme.bg);

    const int x = lv.width() + 6;
    int y = 1;
    grid.print(x, y++, "DUNGEON GENERATOR", theme.accent, theme.bg);
    y++;
    grid.print(x, y++, "Floor " + std::to_string(floorNo), theme.fg, theme.bg);
    grid.print(x, y++, "Seed  " + std::to_string(seed), theme.fg, theme.bg);
    grid.print(x, y++, "Rooms " + std::to_string(lv.rooms().size()), theme.fg, theme.bg);
    grid.print(x, y++, "Doors " + std::to_string(lv.entrances().size()), theme.fg, theme.bg);
    y++;
    const struct { const char* name; TileClass cls; } legend[] = {
        {"wall", TileClass::WALL}, {"floor", TileClass::FLOOR}, {"door", TileClass::C_DOOR},
        {"open door", TileClass::O_DOOR}, {"elevator", TileClass::ELEVATOR}, {"boss", TileClass::BOSS},
        {"treasure", TileClass::TREASURE}, {"mineral vein", TileClass::VEIN}};
    for (const auto& l : legend) {
      const Look lk = lookFor(l.cls, theme, showSecret);
      grid.put(x, y, lk.glyph, lk.fg, theme.bg);
      grid.print(x + 2, y++, l.name, theme.muted, theme.bg);
    }
    const Look sk = lookFor(TileClass::SECRET, theme, true);
    grid.put(x, y, sk.glyph, sk.fg, theme.bg);
    grid.print(x + 2, y++, showSecret ? "secret door (S hides)" : "secret door (S shows)", theme.muted, theme.bg);
    y++;
    grid.print(x, y++, "Enter  new dungeon", theme.fg, theme.bg);
    grid.print(x, y++, "Up/Dn  floor +/-", theme.fg, theme.bg);
    grid.print(x, y++, "S      secret door", theme.fg, theme.bg);
    grid.print(x, y++, "Esc    quit", theme.fg, theme.bg);
    drawStatusTag(grid, theme, settingsText(backend->fontInfo()));
    backend->present(grid);
  }
  return 0;
}

}  // namespace laby
