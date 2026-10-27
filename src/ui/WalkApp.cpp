#include <map>
#include <utility>
#include "ui/WalkApp.hpp"

#include <cstdlib>
#include <iostream>
#include <random>

#include "audio/AudioPlayer.hpp"
#include "core/DataFile.hpp"
#include "core/Tuning.hpp"

#include "core/Settings.hpp"
#include "ui/KeyMap.hpp"
#include "ui/ScreenLayout.hpp"
#include "ui/StatusTag.hpp"
#include "ui/TermLayout.hpp"

namespace laby {

int runWalkView(std::uint64_t seed) {
  settings().load();
  settings().saveEnabled = true;
  setUserThemeColors(settings().themeColors);
  Theme theme = loadTheme();
  ThemeWatcher themeWatcher;
  auto backend = makeBackend(kGridCols, kGridRows, "LabyTerm: walk");
  terminalUi() = backend->isTerminal();
  int gridCols = kGridCols, gridRows = kGridRows;
  const auto wantedGrid = [&] {  // a terminal's grid is its own size (at least the minimum); the window's is fixed
    if (!backend->preferredGrid(gridCols, gridRows)) return;
    gridCols = std::max(gridCols, kTermMinCols);
    gridRows = std::max(gridRows, kTermMinRows);
  };
  wantedGrid();
  CellGrid grid(gridCols, gridRows);
  int appliedCrt = backend->crtLevel();
  if (settings().crt >= 0 && !std::getenv("LABYTERM_CRT")) backend->setCrtLevel(settings().crt);
  appliedCrt = settings().crt = backend->crtLevel();
  WalkSession session(seed);
  session.setStateDir(stateDir());
  session.noteLaunchMode(backend->isTerminal());
  session.startTitle();
#ifdef LABYTERM_DEBUG_KEYS
  if (std::getenv("LABYTERM_SKIP_TITLE")) { session.newRun(seed); session.handle(Key::Enter); session.handle(Key::Enter); }  // straight into a dungeon (to measure the CPU use of the torch)
#endif
  std::random_device rd;
  std::string audioProblem;
  auto audio = AudioPlayer::create(dataDir(), &audioProblem);  // quiet (with a note) when there is no sound device
  if (!audio) std::cerr << "labyterm: running without sound (" << audioProblem << ")\n";
  if (!backend->hasFocus()) {  // opened hidden or in the background: no Focus event will come until it is shown
    session.setFocused(false);
    if (audio) audio->setFocused(false);
  }

  bool dirty = true, moving = false;
  std::uint64_t lastMoveMs = 0;
  std::map<std::pair<Key, char32_t>, std::uint64_t> lastDown;  // key -> time of its last key-down
  for (bool running = true; running;) {
    Event e;
    const std::uint64_t wake = std::min<std::uint64_t>(1000, session.torchWakeMs());  // the torch's next flicker (never, while unfocused)
    for (bool have = backend->waitEvent(e, moving ? (session.onStormScreen() ? 33 : 16) : static_cast<int>(wake)); have; have = backend->pollEvent(e)) {
      dirty = true;
      if (e.type == Event::Type::Quit) running = false;
      if (e.type == Event::Type::Focus) {
        session.setFocused(e.focused);
        if (audio) audio->setFocused(e.focused);
        continue;
      }
      if (e.type == Event::Type::Text) {
        session.textInput(e.text);
        continue;
      }
      if (e.type != Event::Type::KeyDown) continue;
      // Held keys arrive from the compositor as release+press pairs ~25 ms apart, with no repeat flag, so a repeat is told
      // by timing: the same key again within 100 ms of its last event (a human can't tap that fast).
      const std::uint64_t now = backend->ticksMs();
      {
        auto [it, fresh] = lastDown.try_emplace({e.key, e.text}, 0);
        if (!fresh && now - it->second < 100) e.repeat = true;
        it->second = now;
      }
      if (session.editingText()) {  // the name field: the characters arrive as Text events
        if (e.key == Key::Backspace || e.key == Key::Enter || e.key == Key::Escape || e.key == Key::Up || e.key == Key::Down) session.handle(e.key, 0, false);
        continue;
      }
      if (session.capturing()) {  // the key-bindings page wants the raw key
        if (!e.repeat) session.captureKey(e.key, e.text);
        continue;
      }
      Key key = e.key;
      char32_t text = e.text;
      if (!translateKey(e.key, e.text, key, text)) continue;  // a key the player unbound
      if (e.repeat) {  // a held key: no faster than the key-repeat setting, measured from the last step it made
        const int ms = settings().keyRepeat >= 0 ? settings().keyRepeat : tuning().input.keyRepeatMs;
        if (now < lastMoveMs + static_cast<std::uint64_t>(ms)) continue;
      }
      lastMoveMs = now;
      if (text == 'a' && !session.inCombat()) {  // the audio key (in a fight A is Attack): Full Audio -> Effects Only -> Full Mute (the Options switches follow)
        cycleSoundMode(settings());
        settings().save();
        dirty = true;
        continue;
      }
      session.handle(key, text, e.repeat);
    }
    if (session.quitRequested()) running = false;
    if (settings().crt != appliedCrt) {  // chosen in Options
      backend->setCrtLevel(settings().crt);
      appliedCrt = settings().crt = backend->crtLevel();
    } else if (backend->crtLevel() != appliedCrt) {  // changed by the window's own hotkey
      appliedCrt = settings().crt = backend->crtLevel();
      settings().save();
    }
    if (themeWatcher.poll(theme, backend->ticksMs())) dirty = true;
    moving = session.tick(backend->ticksMs());  // the fight camera easing in or out
    if (audio) {
      const auto& a = tuning().audio;
      const Settings& st = settings();
      audio->setVolumes(a.master * st.master / 10.0, st.music ? a.music * st.musicVol / 10.0 : 0.0, st.sfx ? a.sfx * st.sfxVol / 10.0 : 0.0);
      audio->handle(session.takeAudio());
      audio->update();
    } else {
      session.takeAudio();
    }
    const bool torchChanged = session.takeTorchDirty();
    if (moving) dirty = true;
    if (torchChanged && !dirty && session.updateTorchOnly(grid)) {  // the breathing: only the torch's numbers changed, so the picture is not drawn again
      backend->present(grid);
      continue;
    }
    if (torchChanged) dirty = true;
    if (backend->isTerminal()) {
      const int oldCols = gridCols, oldRows = gridRows;
      wantedGrid();
      if (gridCols != oldCols || gridRows != oldRows) {
        grid = CellGrid(gridCols, gridRows);
        dirty = true;
      }
    }
    if (!dirty && !backend->needsFrame()) continue;
    dirty = false;
    session.draw(grid, theme, "seed " + std::to_string(session.seed()));
    backend->present(grid);
  }
  return 0;
}

}  // namespace laby
