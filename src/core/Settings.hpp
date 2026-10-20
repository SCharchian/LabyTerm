#pragma once
#include <array>
#include <string>

namespace laby {

// What the player can change from the Options menu (as opposed to tuning, which is game balance and lives in data/gameplay.json and
// ~/.config/labyterm/tuning.json). Saved to $LABYTERM_SETTINGS, else $XDG_CONFIG_HOME/labyterm/settings.json, else
// ~/.config/labyterm/settings.json, whenever something changes (only when the app turned saving on: headless tools never write it).

// The actions whose keys can be rebound. Escape (menu / back) and the combat letter shortcuts are fixed.
enum class Action { Forward, Back, TurnLeft, TurnRight, Confirm, Cancel, Scrolls, Inventory, Map, Audio, Count };
constexpr int kActionCount = static_cast<int>(Action::Count);
constexpr int kKeySlots = 2;  // each action can have two keys

struct Settings {
  bool music = true, sfx = true;            // switches on top of the volumes
  int master = 10, musicVol = 10, sfxVol = 10;  // tenths of what tuning says (10 = as tuned)
  bool themeColors = true;                  // take the colors from the Omarchy theme
  int crt = -1;                             // CRT filter level 0..3 (-1: not chosen yet, the backend's own default)
  int mazeStyle = 1;                        // how the dungeon view is drawn: 0 = octant mosaic (pixels), 1 = text (ASCII characters, the default)
  int keyRepeat = -1;                       // ms between repeats of a held key (-1: tuning's input.keyRepeatMs)
  std::array<std::array<std::string, kKeySlots>, kActionCount> keys;  // key names, "" = empty (see ui/KeyMap)
  std::string playerName;                   // the last character name (offered again on the creation screen)
  bool saveEnabled = false;

  Settings() { resetKeys(); }
  void resetKeys();
  void load();        // missing or broken files leave the defaults
  void save() const;  // no-op unless saveEnabled
};

// The S key's three states, kept in the two switches the Options menu shows: Full Audio (music and effects), Effects Only, Full Mute.
// (Music alone, chosen in Options, is a fourth state that the key moves on from to Full Mute... it reads "Music Only".)
const char* soundModeLabel(const Settings& s);
void cycleSoundMode(Settings& s);  // Full Audio -> Effects Only -> Full Mute -> Full Audio

// The one live copy.
Settings& settings();
std::string settingsPath();

// Names and default keys of the actions (for the file and the key-bindings page).
const char* actionId(Action a);
const char* actionLabel(Action a);
const std::array<std::string, kKeySlots>& defaultKeys(Action a);

}  // namespace laby
