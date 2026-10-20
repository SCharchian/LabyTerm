// Key bindings and the Esc menu: translation, rebinding with conflicts, settings round trip, and walking through the pages headlessly.
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "ui/KeyMap.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);           \
      return 1;                                                         \
    }                                                                   \
  } while (0)

int main() {
  const std::string path = (std::filesystem::temp_directory_path() / "labyterm_menu_test_settings.json").string();
  std::filesystem::remove(path);
  setenv("LABYTERM_SETTINGS", path.c_str(), 1);

  Key k;
  char32_t c;
  // defaults: arrows and Z pass as themselves; Z confirms; S is the Scrolls letter, A the Audio one, M the map
  CHECK(translateKey(Key::Up, 0, k, c) && k == Key::Up);
  CHECK(translateKey(Key::A, U'z', k, c) && k == Key::Enter);
  CHECK(translateKey(Key::Other, U's', k, c) && k == Key::Other && c == U's');
  CHECK(translateKey(Key::Other, U'a', k, c) && c == U'a' && translateKey(Key::Other, U'm', k, c) && c == U'm');
  // rebind Scrolls to q: q works, s no longer does, and an unrelated letter passes
  CHECK(bindKey(Action::Scrolls, 0, "q").empty());
  CHECK(translateKey(Key::Other, U'q', k, c) && c == U's');
  CHECK(!translateKey(Key::Other, U's', k, c));
  CHECK(translateKey(Key::Other, U'a', k, c) && c == U'a');
  // taking a key from another action says so and unbinds it there
  CHECK(bindKey(Action::Inventory, 0, "q") == "Scrolls");
  CHECK(settings().keys[static_cast<size_t>(Action::Scrolls)][0].empty());
  CHECK(translateKey(Key::Other, U'q', k, c) && c == U'i');
  // the sound key: Full Audio -> Effects Only -> Full Mute -> Full Audio, through the Options switches
  {
    Settings st;
    CHECK(std::string(soundModeLabel(st)) == "Full Audio");
    cycleSoundMode(st);
    CHECK(std::string(soundModeLabel(st)) == "Effects Only" && !st.music && st.sfx);
    cycleSoundMode(st);
    CHECK(std::string(soundModeLabel(st)) == "Full Mute" && !st.music && !st.sfx);
    cycleSoundMode(st);
    CHECK(std::string(soundModeLabel(st)) == "Full Audio");
  }
  // Escape can't be bound
  CHECK(keyName(Key::Escape, 0).empty());

  // save and load again
  settings().saveEnabled = true;
  settings().music = false;
  settings().master = 4;
  settings().save();
  settings().resetKeys();
  settings().music = true;
  settings().master = 10;
  settings().load();
  CHECK(!settings().music && settings().master == 4);
  CHECK(settings().keys[static_cast<size_t>(Action::Inventory)][0] == "q");
  settings().saveEnabled = false;

  // the Esc menu opens, pages through everything and quits only after the question
  WalkSession s(3);
  CHECK(!s.menuOpen());
  s.handle(Key::Escape);
  CHECK(s.menuOpen() && !s.quitRequested());
  s.handle(Key::Down); s.handle(Key::Enter);   // Help
  s.handle(Key::Right); s.handle(Key::Down);   // FAQ, scroll
  s.handle(Key::Escape);                       // back to the main page
  s.handle(Key::Down); s.handle(Key::Enter);   // About: the Help page on its About/AI tab
  s.handle(Key::Escape);
  s.handle(Key::Down); s.handle(Key::Enter);   // Abandon run?
  s.handle(Key::Escape);                       // back out
  s.handle(Key::Down); s.handle(Key::Enter);   // Quit?
  CHECK(!s.quitRequested());
  s.handle(Key::Escape);                       // back out
  s.handle(Key::Up); s.handle(Key::Up); s.handle(Key::Up); s.handle(Key::Up); s.handle(Key::Up);  // wraps around
  s.handle(Key::Escape);
  CHECK(!s.menuOpen());
  s.handle(Key::Escape);
  for (int i = 0; i < 4; ++i) s.handle(Key::Down);
  s.handle(Key::Enter);                        // Quit? (No is chosen)
  s.handle(Key::Up);                           // Yes
  s.handle(Key::Enter);
  CHECK(s.quitRequested());

  // abandon run: confirms, then drops to the title
  WalkSession ab(3);
  ab.startTitle(); ab.handle(Key::Enter);  // Enter the Labyrinth
  ab.handle(Key::Enter);
  CHECK(ab.inGame());
  ab.handle(Key::Escape);
  for (int i = 0; i < 3; ++i) ab.handle(Key::Down);
  ab.handle(Key::Enter); ab.handle(Key::Up); ab.handle(Key::Enter);
  CHECK(ab.onTitle() && !ab.quitRequested());

  // with no run started, Abandon run is skipped and cannot be picked
  ab.handle(Key::Escape);
  CHECK(ab.menuOpen());
  for (int i = 0; i < 3; ++i) ab.handle(Key::Down);   // About, then past Abandon run to Quit
  ab.handle(Key::Enter);
  ab.handle(Key::Up); ab.handle(Key::Enter);
  CHECK(ab.quitRequested());

  // options: a toggle changes the setting, and the bindings page captures a raw key
  WalkSession t(3);
  settings().resetKeys();
  t.handle(Key::Escape);
  t.handle(Key::Enter);                        // Options
  t.handle(Key::Enter);                        // Music toggles
  CHECK(settings().music);                     // (was off from the file, now on)
  for (int i = 0; i < 7; ++i) t.handle(Key::Down);
  CHECK(settings().mazeStyle == 1);
  t.handle(Key::Right);                        // Dungeon style: Pixel Art
  CHECK(settings().mazeStyle == 0);
  t.handle(Key::Left);
  CHECK(settings().mazeStyle == 1);
  for (int i = 0; i < 2; ++i) t.handle(Key::Down);
  t.handle(Key::Enter);                        // Key bindings
  t.handle(Key::Enter);                        // rebind Move forward
  CHECK(t.capturing());
  t.captureKey(Key::Other, U'w');
  CHECK(!t.capturing() && settings().keys[0][0] == "w");
  // level-up: one perk per level; a chosen stat goes up by 1, <Random> by 2 in all
  {
    WalkSession l(5);
    const double dex = l.hero().base[1];
    const int level = l.hero().level;
    l.debugLevelUp();
    CHECK(l.levelingUp() && l.hero().level == level + 1);
    l.handle(Key::Down);                    // Dexterity
    l.handle(Key::Enter);
    CHECK(!l.levelingUp() && l.hero().base[1] == dex + 1);
    double sum0 = 0;
    for (double b : l.hero().base) sum0 += b;
    l.debugLevelUp();
    l.handle(Key::Up);                      // wraps to <Random> +2
    l.handle(Key::Enter);
    double sum1 = 0;
    for (double b : l.hero().base) sum1 += b;
    CHECK(!l.levelingUp() && sum1 == sum0 + 2);
    // two levels at once: two perks in a row
    l.debugLevelUp();
    l.handle(Key::Enter);
    CHECK(!l.levelingUp());
  }
  // start screen -> character creation -> game
  {
    settings().playerName.clear();
    WalkSession g(5);
    g.startTitle();
    CHECK(g.onTitle() && !g.inGame());
    g.handle(Key::Enter);                   // Enter the Labyrinth
    CHECK(g.onCreation());
    g.handle(Key::Down);                    // wraps to the name: the field takes focus by itself
    CHECK(g.editingText());
    g.textInput(U'S'); g.textInput(U'a'); g.textInput(U'm');
    g.handle(Key::Backspace);
    g.textInput(U'x');
    g.handle(Key::Enter);                   // done
    CHECK(!g.editingText() && g.hero().name == "Sax");
    // focus moved on to Re-Roll Character by itself: stats and item together
    g.handle(Key::Enter);
    CHECK(g.hero().hp == g.hero().maxHP);
    int things = static_cast<int>(g.hero().scrolls.size());
    for (const auto& e : g.hero().equipment) things += e.has_value();
    CHECK(things == 1);                     // still exactly one starting item
    g.handle(Key::Down);
    g.handle(Key::Enter);                   // Enter the Labyrinth!
    CHECK(g.inGame() && g.hero().name == "Sax" && settings().playerName == "Sax");
    // no name typed: the original's default
  {
    WalkSession d(6);
    settings().playerName.clear();
    d.startTitle();
    d.handle(Key::Enter);
    d.handle(Key::Enter);                   // Enter the Labyrinth! is under the cursor
    CHECK(d.inGame() && d.hero().name == "D. Crawley");
    WalkSession e(6);
    e.startTitle();
    e.handle(Key::Enter);
    e.handle(Key::Down);                    // wraps to the name
                                            // the field has focus; type nothing
    e.handle(Key::Enter);                   // the default name is filled in and Re-Roll has focus
    CHECK(e.hero().name == "D. Crawley");
    e.handle(Key::Down);                    // Enter the Labyrinth!
    e.handle(Key::Enter);
    CHECK(e.inGame() && e.hero().name == "D. Crawley");
  }
  // options and help open from the title, and Esc closes them again
    WalkSession h(5);
    h.startTitle();
    h.handle(Key::Down); h.handle(Key::Down); h.handle(Key::Enter);   // Options (after Enter the Labyrinth and Legends)
    CHECK(h.menuOpen());
    h.handle(Key::Escape);
    CHECK(h.onTitle() && h.menuOpen());     // (the title itself keeps menuOpen true)
    h.handle(Key::Down); h.handle(Key::Down); h.handle(Key::Enter);   // Quit
    CHECK(h.quitRequested());
  }
  std::filesystem::remove(path);
  std::printf("menu_test OK\n");
  return 0;
}
