#include "ui/SystemMenu.hpp"

#include <string_view>
#include <fstream>

#include "core/DataFile.hpp"
#include "core/Settings.hpp"
#include "platform/Theme.hpp"
#include "ui/KeyMap.hpp"
#include "ui/PanelKit.hpp"
#include "ui/TermLayout.hpp"

namespace laby {
namespace {

constexpr int kWrapChars = 46;  // characters per line on a Page (see drawPage: the width is 93 cells, 11/6 cells per character)
// The terminal build's text is one cell per character, so a Page (84 cells wide there) and the side notes take more characters per line.
int noteChars() { return terminalUi() ? 38 : 25; }
const char* kTabNames[] = {"How to play", "FAQ", "About/AI"};
const char* kTabFiles[] = {"help/howtoplay.txt", "help/faq.txt", "help/About_and_AI.txt"};
constexpr int kTabCount = 3;

const int kRepeatChoices[] = {-1, 80, 150, 300};
const char* kRepeatNames[] = {"Default", "Fast", "Medium", "Slow"};
const char* kCrtNames[] = {"Off", "Subtle", "Default", "Heavy"};
const char* kMazeNames[] = {"Pixel Art", "Text"};

std::string percent(int tenths) { return std::to_string(tenths * 10) + "%"; }
std::string onOff(bool b) { return b ? "On" : "Off"; }

int repeatIndex() {
  for (int i = 0; i < 4; ++i)
    if (kRepeatChoices[i] == settings().keyRepeat) return i;
  return 0;
}

struct Choice { const char* label; const char* help; };
const Choice kOptionInfo[] = {
    {"Music", "Turns the music on or off."},
    {"Sound effects", "Turns footsteps, fights and other sounds on or off."},
    {"Master volume", "The overall volume."},
    {"Music volume", "The music's volume."},
    {"Effects volume", "The sound effects' volume."},
    {"Omarchy theme colors", "Follow Omarchy theme colors when on"},
    {"CRT filter", "Scanlines & glow effect intensity"},
    {"Dungeon style", "Render the Dungeon as Text or Pixel art"},
    {"Key repeat", "How fast a held key repeats."},
    {"Key bindings", "Choose your own keys. [Enter] opens the list."},
};
constexpr int kOptionCount = 10;

}  // namespace

void SystemMenu::openMain() {
  page_ = Page::Main;
  sel_ = 0;
  capture_ = false;
  direct_ = false;
  click();
}

void SystemMenu::openDirect(Page p) {
  page_ = p;
  sel_ = 0;
  direct_ = true;
  capture_ = false;
  click();
}

void SystemMenu::openHelp() {
  openDirect(Page::Help);
  tab_ = 0;
  scroll_ = 0;
  loadHelp();
}

void SystemMenu::leave(Page to, int sel) {
  if (direct_) close();
  else { page_ = to; sel_ = sel; }
  click();
}

void SystemMenu::apply() const {
  setUserThemeColors(settings().themeColors);
  settings().save();
}

std::vector<SystemMenu::Row> SystemMenu::optionRows() const {
  const Settings& s = settings();
  std::vector<Row> r;
  r.push_back({kOptionInfo[0].label, onOff(s.music)});
  r.push_back({kOptionInfo[1].label, onOff(s.sfx)});
  r.push_back({kOptionInfo[2].label, percent(s.master)});
  r.push_back({kOptionInfo[3].label, percent(s.musicVol), s.music});
  r.push_back({kOptionInfo[4].label, percent(s.sfxVol), s.sfx});
  r.push_back({kOptionInfo[5].label, onOff(s.themeColors)});
  r.push_back({kOptionInfo[6].label, s.crt >= 0 && s.crt <= 3 ? kCrtNames[s.crt] : "Default"});
  r.push_back({kOptionInfo[7].label, kMazeNames[s.mazeStyle ? 1 : 0]});
  r.push_back({kOptionInfo[8].label, kRepeatNames[repeatIndex()]});
  r.push_back({kOptionInfo[9].label, ">"});
  return r;
}

void SystemMenu::optionChange(int row, int dir, bool enter) {
  Settings& s = settings();
  const auto bump = [&](int& v, int lo, int hi) { v = std::clamp(v + dir, lo, hi); };
  switch (row) {
    case 0: s.music = !s.music; break;
    case 1: s.sfx = !s.sfx; break;
    case 2: bump(s.master, 0, 10); break;
    case 3: bump(s.musicVol, 0, 10); break;
    case 4: bump(s.sfxVol, 0, 10); break;
    case 5: s.themeColors = !s.themeColors; break;
    case 6: {
      const int cur = s.crt < 0 ? 2 : s.crt;
      s.crt = enter ? (cur + 1) % 4 : std::clamp(cur + dir, 0, 3);
      break;
    }
    case 7: s.mazeStyle = !s.mazeStyle; break;
    case 8: s.keyRepeat = kRepeatChoices[(repeatIndex() + (enter ? 1 : dir) + 4) % 4]; break;
    case 9:
      if (enter) { page_ = Page::Keys; sel_ = 0; slot_ = 0; note_.clear(); click(); }
      return;
    default: return;
  }
  // toggles flip on either arrow; the sliders stop at their ends
  click();
  apply();
}

void SystemMenu::loadHelp() const {
  if (helpLoaded_) return;
  helpLoaded_ = true;
  help_.assign(kTabCount, {});
  for (int t = 0; t < kTabCount; ++t) {
    std::ifstream in(dataDir() + "/" + kTabFiles[t]);
    if (!in) {
      help_[static_cast<size_t>(t)].push_back({"(the help file is missing)", "", 2});
      continue;
    }
    std::string line;
    while (std::getline(in, line)) {
      auto& out = help_[static_cast<size_t>(t)];
      if (line.empty()) { out.push_back({"", "", 0}); continue; }
      if (line[0] == '#') {  // a heading: bright
        for (const std::string& w : wrap(line.substr(line.find_first_not_of("# ")), pageChars_ > 0 ? pageChars_ : kWrapChars)) out.push_back({w, "", 1});
      } else {
        const bool bullet = line.rfind("- ", 0) == 0;  // a bullet's wrapped lines hang under its text
        const auto parts = wrap(line, pageChars_ > 0 ? pageChars_ : kWrapChars);
        for (size_t i = 0; i < parts.size(); ++i) out.push_back({(bullet && i > 0 ? "  " : "") + parts[i], "", 0});
      }
    }
  }
}

void SystemMenu::captureKey(Key key, char32_t ch) {
  if (key == Key::Escape) {  // cancel
    capture_ = false;
    note_ = "Cancelled.";
    return;
  }
  const std::string name = keyName(key, ch);
  if (name.empty()) return;  // not a key that can be bound: keep waiting
  const std::string taken = bindKey(static_cast<Action>(sel_), slot_, name);
  note_ = taken.empty() ? "Bound to " + keyDisplay(name) + "." : keyDisplay(name) + " moved here from " + taken + ".";
  capture_ = false;
  click();
  apply();
}

void SystemMenu::key(Key key, char32_t ch) {
  const bool back = key == Key::Escape || key == Key::B || ch == U'x';
  const bool ok = key == Key::Enter || key == Key::A;
  const bool up = key == Key::Up, down = key == Key::Down, left = key == Key::Left, right = key == Key::Right;
  const auto wrapSel = [&](int n) {
    if (up) { sel_ = (sel_ + n - 1) % n; click(); }
    if (down) { sel_ = (sel_ + 1) % n; click(); }
  };
  switch (page_) {
    case Page::None: return;
    case Page::Main:
      if (back) { close(); click(); return; }
      wrapSel(5);
      if (sel_ == 3 && !canAbandon()) {  // greyed out: the cursor steps over it
        if (up) sel_ = 2;
        else if (down) sel_ = 4;
      }
      if (ok && (sel_ != 3 || canAbandon())) {
        click();
        if (sel_ == 0) { page_ = Page::Options; sel_ = 0; }
        else if (sel_ == 1 || sel_ == 2) {  // Help, and About: the Help page opened on its About/AI tab
          helpFrom_ = sel_;
          page_ = Page::Help; tab_ = sel_ == 1 ? 0 : kTabCount - 1; scroll_ = 0; loadHelp();
        }
        else if (sel_ == 3) { page_ = Page::Abandon; sel_ = 1; }  // "No" is under the cursor
        else { page_ = Page::Quit; sel_ = 1; }
      }
      return;
    case Page::Options:
      if (back) { leave(Page::Main, 0); return; }
      wrapSel(kOptionCount);
      if (left || right) optionChange(sel_, left ? -1 : 1, false);
      else if (ok) optionChange(sel_, 1, true);
      return;
    case Page::Keys:
      if (back) { page_ = Page::Options; sel_ = kOptionCount - 1; note_.clear(); click(); return; }
      wrapSel(kActionCount + 1);
      if (left || right) { slot_ ^= 1; click(); }
      if (ok) {
        if (sel_ == kActionCount) { settings().resetKeys(); note_ = "Keys reset to the defaults."; apply(); click(); }
        else { capture_ = true; note_.clear(); click(); }
      }
      if (key == Key::Backspace && sel_ < kActionCount) {
        settings().keys[static_cast<size_t>(sel_)][static_cast<size_t>(slot_)].clear();
        note_ = "Key cleared.";
        apply();
        click();
      }
      return;
    case Page::Help:
      if (back) { leave(Page::Main, helpFrom_); return; }
      if (left || right) {
        tab_ = (tab_ + (left ? kTabCount - 1 : 1)) % kTabCount;
        scroll_ = 0;
        click();
      }
      if (up && scroll_ > 0) --scroll_;
      if (down) {
        const int total = static_cast<int>(help_[static_cast<size_t>(tab_)].size());
        if (scroll_ < total - kPageLines) ++scroll_;
      }
      return;
    case Page::Abandon:
      if (back) { page_ = Page::Main; sel_ = 3; click(); return; }
      if (up || down || left || right) { sel_ ^= 1; click(); }
      if (ok) {
        if (sel_ == 0) { close(); if (abandon_) abandon_(); }
        else { page_ = Page::Main; sel_ = 3; click(); }
      }
      return;
    case Page::Quit:
      if (back) { page_ = Page::Main; sel_ = 4; click(); return; }
      if (up || down || left || right) { sel_ ^= 1; click(); }
      if (ok) {
        if (sel_ == 0) quit_ = true;
        else { page_ = Page::Main; sel_ = 4; click(); }
      }
      return;
  }
}

std::string SystemMenu::hints() const {
  switch (page_) {
    case Page::Main: return "[Arrows] Choose  [Enter/Z] Select  [Esc] Resume";
    case Page::Options: return "[Arrows] Choose/Change  [Esc] Back";
    case Page::Keys: return capture_ ? "Press the new key  [Esc] Cancel" : "[Arrows] Choose/Slot  [Enter/Z] Rebind  [Bksp] Clear  [Esc] Back";
    case Page::Help: return "[Arrows] Scroll/Page  [Esc] Back";
    case Page::Abandon:
    case Page::Quit: return "[Arrows] Choose  [Enter/Z] Select  [Esc] Back";
    default: return "";
  }
}

void SystemMenu::build(MenuPanel& m) const {
  m.place = MenuPanel::Place::Left;
  m.selected = sel_;
  switch (page_) {
    case Page::None: return;
    case Page::Main:
      m.title = "MENU";
      for (const char* t : {"Options", "Help", "About", "Abandon run", "Quit LabyTerm"})
        m.rows.push_back({t, "", 0, std::string_view(t) != "Abandon run" || canAbandon()});
      if (sel_ == 3) for (const std::string& l : wrap("Return to Main Menu (Character will be lost)", noteChars())) m.detail.push_back({l, "", 0});
      m.footer = "[Enter] Pick  [Esc] Resume";
      return;
    case Page::Options: {
      m.title = "OPTIONS";
      const auto rows = optionRows();
      for (int i = 0; i < kOptionCount; ++i) {
        const Row& r = rows[static_cast<size_t>(i)];
        const bool on = i == sel_, arrows = i != kOptionCount - 1 && on;
        m.rows.push_back({r.label, arrows ? "< " + r.value + " >" : r.value, 0, r.enabled});
      }
      for (const std::string& l : wrap(kOptionInfo[sel_].help, noteChars())) m.detail.push_back({l, "", 0});
      m.footer = "[Arrows] Change";
      return;
    }
    case Page::Keys: {
      m.title = "KEY BINDINGS";
      for (int a = 0; a < kActionCount; ++a) {
        const auto& k = settings().keys[static_cast<size_t>(a)];
        std::string v[2];
        for (int s = 0; s < 2; ++s) {
          v[s] = keyDisplay(k[static_cast<size_t>(s)]);
          if (a == sel_ && s == slot_) v[s] = capture_ ? "[ ? ]" : "< " + v[s] + " >";
        }
        v[0].resize(std::max<size_t>(6, v[0].size()), ' ');
        v[1].resize(std::max<size_t>(2, v[1].size()), ' ');
        m.rows.push_back({actionLabel(static_cast<Action>(a)), v[0] + "  " + v[1], 0, true});
      }
      m.rows.push_back({"Reset to defaults", "", 0, true});
      if (capture_) m.detail.push_back({"Press the new key for", "", 1}), m.detail.push_back({std::string(actionLabel(static_cast<Action>(sel_))) + ".", "", 1}), m.detail.push_back({"Esc cancels.", "", 2});
      else {
        for (const std::string& l : wrap(note_, noteChars())) m.detail.push_back({l, "", 1});
      }
      m.footer = capture_ ? "Waiting for a key..." : "[Enter] Rebind";
      return;
    }
    case Page::Help:
      m.place = MenuPanel::Place::Page;
      m.title = "HELP";
      for (int t = 0; t < kTabCount; ++t) m.tabs.push_back(kTabNames[t]);
      m.tab = tab_;
      m.scroll = scroll_;
      loadHelp();  // (again when the page width changed)
      m.pageLines = help_[static_cast<size_t>(tab_)];
      m.footer = "[Arrows] Scroll/Page  [Esc] Back";
      return;
    case Page::Abandon:
      m.title = "ABANDON";
      m.rows = {{"Yes, abandon this run", "", 0, true}, {"No, keep playing", "", 0, true}};
      m.detail.push_back({"Return to Main Menu", "", 2});
      m.detail.push_back({"(Character will be lost)", "", 2});
      m.footer = "[Enter] Pick  [Esc] Back";
      return;
    case Page::Quit:
      m.title = "QUIT";
      m.rows = {{"Yes, quit LabyTerm", "", 0, true}, {"No, keep playing", "", 0, true}};
      m.detail.push_back({"Character will be lost,", "", 2});
      m.detail.push_back({"you can save in town.", "", 2});
      m.footer = "[Enter] Pick  [Esc] Back";
      return;
  }
}

}  // namespace laby
