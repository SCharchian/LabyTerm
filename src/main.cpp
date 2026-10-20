// LabyTerm: a spiritual port of the Playdate game Labyrinth, drawn as colored text.
// This is the scaffold's smoke test: theme colors, box drawing, key echo, Up/Down scroll.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

#include "core/DataFile.hpp"
#include "core/Enums.hpp"
#include "core/Tuning.hpp"
#include "platform/Theme.hpp"
#include "ui/FontShowcase.hpp"
#include "ui/LayoutVariants.hpp"
#include "ui/MapViewer.hpp"
#include "ui/WalkApp.hpp"
#include "render/Backend.hpp"

using namespace laby;

// `labyterm --check-data`: load every data file, check that every name in it maps to a generated
// enum, and print counts (no window). Exit 1 on any problem.
template <class E>
static E need(const nlohmann::json& v, const std::string& where) {
  auto e = fromString<E>(v.get<std::string>());
  if (!e) throw std::runtime_error(where + ": unknown name '" + v.get<std::string>() + "'");
  return *e;
}

static void checkStats(const nlohmann::json& bonuses, const std::string& where, bool allowHpMp) {
  for (auto& [k, v] : bonuses.items()) {
    if (allowHpMp && (k == "HP" || k == "MP")) continue;
    need<Stat>(k, where);
  }
}

static int checkData() {
  try {
    auto eq = loadDataFile("equipment.json");
    size_t items = 0;
    for (auto& [type, list] : eq["items"].items()) {
      need<EquipmentType>(type, "equipment.items");
      for (auto& it : list) {
        ++items;
        need<EquipmentType>(it["Type"], "equipment item " + it["Name"].get<std::string>());
        if (it.contains("WeaponType")) need<WeaponType>(it["WeaponType"], "weapon " + it["Name"].get<std::string>());
        checkStats(it["Bonuses"], "equipment item " + it["Name"].get<std::string>(), true);
      }
    }
    size_t prefixes = 0;
    for (auto& [kind, list] : eq["prefixes"].items())
      for (auto& p : list) { ++prefixes; checkStats(p["Bonuses"], "equipment prefix " + p["Name"].get<std::string>(), true); }

    auto mon = loadDataFile("monsters.json");
    size_t monsters = 0;
    for (auto& [cls, list] : mon["classes"].items()) {
      auto c = need<MonsterClass>(cls, "monsters.classes");
      for (auto& m : list) {
        ++monsters;
        if (need<MonsterClass>(m["Class"], "monster " + m["DisplayName"].get<std::string>()) != c)
          throw std::runtime_error("monster " + m["DisplayName"].get<std::string>() + " is filed under the wrong class");
        checkStats(m["stats"], "monster " + m["DisplayName"].get<std::string>(), false);
      }
    }
    for (auto& p : mon["prefixes"]) checkStats(p["stats"], "monster prefix " + p["Name"].get<std::string>(), false);

    auto sc = loadDataFile("scrolls.json");
    for (auto& s2 : sc["scrolls"]) {
      need<ScrollType>(s2["ScrollType"], "scroll " + s2["Name"].get<std::string>());
      need<EquipmentType>(s2["Type"], "scroll " + s2["Name"].get<std::string>());
    }
    for (auto& [name, id] : sc["scrollTypes"].items()) {  // JSON ids are the original 1-based Lua values
      auto e = need<ScrollType>(name, "scrollTypes");
      if (static_cast<int>(e) + 1 != id.get<int>())
        throw std::runtime_error("scrollTypes." + name + ": id " + std::to_string(id.get<int>()) + " != enum order");
    }

    TuningLoad tl = loadTuning();  // throws if gameplay.json and TuningParams.hpp are out of sync
    if (!tl.overrideError.empty()) throw std::runtime_error("tuning override: " + tl.overrideError);

    std::cout << "equipment.json: " << items << " items, " << prefixes << " prefixes\n"
              << "monsters.json:  " << monsters << " monsters, " << mon["prefixes"].size() << " prefixes\n"
              << "scrolls.json:   " << sc["scrolls"].size() << " scrolls\n"
              << "gameplay.json:  " << kTuningParamCount << " tuning knobs (dungeon " << tl.values.dungeon.sizeX << "x"
              << tl.values.dungeon.sizeY << ", " << tl.overridden.size() << " overridden)\n"
              << "all names resolve to generated enums\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "data error: " << e.what() << "\n";
    return 1;
  }
}

static int run(int argc, char** argv) {
  if (argc > 1 && (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-V") == 0)) {
    std::cout << "labyterm " LABYTERM_VERSION "\n";
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "--check-data") == 0) return checkData();
  if (argc > 1 && std::strcmp(argv[1], "--fonts") == 0) return runFontShowcase();
  if (argc > 1 && std::strcmp(argv[1], "--layouts") == 0)
    return runLayoutVariants(argc > 2 && std::isdigit(static_cast<unsigned char>(argv[2][0])) ? std::atoi(argv[2]) : 1,
                             argc > 2 && std::strcmp(argv[argc - 1], "combat") == 0);
  // The game: no arguments (or --walk [seed]) starts a new run; a seed makes the dungeon repeatable.
  if (argc == 1 || std::strcmp(argv[1], "--walk") == 0 || std::isdigit(static_cast<unsigned char>(argv[1][0]))) {
    const bool bare = argc > 1 && argv[1][0] != '-';  // `labyterm 42`
    const char* seedArg = bare ? argv[1] : argc > 2 ? argv[2] : nullptr;
    std::random_device rd;
    return runWalkView(seedArg ? std::strtoull(seedArg, nullptr, 10) : (static_cast<std::uint64_t>(rd()) << 16) ^ rd());
  }
  if (argc > 1 && std::strcmp(argv[1], "--map") == 0)
    return runMapViewer(argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1);
  if (argc > 1 && std::strcmp(argv[1], "--tuning") == 0) {
    try {
      std::cout << describeTuning(loadTuning());
      return 0;
    } catch (const std::exception& e) {
      std::cerr << "tuning error: " << e.what() << "\n";
      return 1;
    }
  }

  if (std::strcmp(argv[1], "--smoke") != 0) {
    std::cerr << "unknown option " << argv[1] << "\nusage: labyterm [seed] | --version | --walk [seed] | --map [seed] | --fonts | --layouts [1-3] [combat] | --tuning | --check-data | --smoke\n";
    return 2;
  }
  constexpr int kGridW = 80, kGridH = 30;  // logical grid; the window scales it
  Theme theme = loadTheme();
  ThemeWatcher themeWatcher;
  TuningWatcher tuningWatcher;
  auto backend = makeBackend(kGridW, kGridH, "LabyTerm");
  CellGrid grid(kGridW, kGridH);

  std::string lastKey = "(none)";
  int scroll = 0;

  bool dirty = true;
  for (bool running = true; running;) {
    Event e;
    // Sleep until input, a window event, or a timer (1 s theme/tuning check, toast expiry): an idle game uses ~0% CPU
    const int wakeMs = 1000;
    for (bool have = backend->waitEvent(e, wakeMs); have; have = backend->pollEvent(e)) {
      dirty = true;
      if (e.type == Event::Type::Quit) running = false;
      if (e.type == Event::Type::KeyDown) {
        switch (e.key) {
          case Key::Escape: running = false; break;
          case Key::Up: --scroll; lastKey = "Up"; break;
          case Key::Down: ++scroll; lastKey = "Down"; break;
          case Key::Left: lastKey = "Left"; break;
          case Key::Right: lastKey = "Right"; break;
          case Key::A: lastKey = "A (Z)"; break;
          case Key::B: lastKey = "B (X)"; break;
          case Key::Enter: lastKey = "Enter"; break;
          default: lastKey = "other"; break;
        }
      }
    }

    if (themeWatcher.poll(theme, backend->ticksMs())) {
      lastKey = "(theme reloaded)";
      dirty = true;
    }
    if (tuningWatcher.poll(backend->ticksMs())) {
      lastKey = "(tuning reloaded)";
      dirty = true;
    }

    if (!dirty && !backend->needsFrame()) continue;
    dirty = false;

    grid.clear(theme.bg);
    grid.box(0, 0, kGridW, kGridH, theme.green, theme.bg);
    grid.print(3, 0, " L A B Y T E R M ", theme.accent, theme.bg);
    grid.print(3, 3, "Scaffold OK. Press keys; Esc quits.", theme.fg, theme.bg);
    grid.print(3, 5, "Last key: " + lastKey, theme.cyan, theme.bg);
    grid.print(3, 6, "Scroll:   " + std::to_string(scroll), theme.cyan, theme.bg);
    grid.print(3, 8, theme.fromOmarchy ? "Theme: Omarchy (" + theme.name + ")"
                                         : std::string("Theme: built-in fallback"),
               theme.muted, theme.bg);
    // live tuning readout (edit ~/.config/labyterm/tuning.json while this runs)
    grid.print(3, 14, "Tuning: " + std::to_string(tuningWatcher.overriddenCount()) + " overridden;  combat.baseToHit = " +
                          std::to_string(tuning().combat.baseToHit),
               theme.fg, theme.bg);
    if (!tuningWatcher.lastError().empty()) {
      const std::string msg = "Tuning file rejected (keeping last good values): " + tuningWatcher.lastError();
      grid.print(3, 15, msg.substr(0, 72), theme.red, theme.bg);
      if (msg.size() > 72) grid.print(3, 16, msg.substr(72, 72), theme.red, theme.bg);
    }
    // 16 ANSI swatches from the theme
    for (int i = 0; i < 16; ++i)
      grid.fill(3 + i * 4, 10, 4, 2, U'█', theme.ansi[i], theme.bg);
    backend->present(grid);
  }
  return 0;
}

// Anything that escapes (no display, missing or damaged data directory, ...) ends with a one-line message, not a core dump.
int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "labyterm: " << e.what() << "\n"
              << "(game data is read from $LABYTERM_DATA, else <exe>/../share/labyterm, else the source tree it was built from; looking in " << laby::dataDir() << "; a window needs a display)\n";
    return 1;
  }
}
