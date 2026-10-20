#include "core/Settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace laby {
namespace {

namespace fs = std::filesystem;

struct ActionInfo {
  const char* id;
  const char* label;
  std::array<std::string, kKeySlots> keys;
};

const ActionInfo kActions[kActionCount] = {
    {"forward", "Move forward", {"Up", ""}},   {"back", "Move back", {"Down", ""}},
    {"turnLeft", "Turn left", {"Left", ""}},   {"turnRight", "Turn right", {"Right", ""}},
    {"confirm", "Confirm", {"Enter", "z"}},    {"cancel", "Cancel", {"x", ""}},
    {"scrolls", "Scrolls", {"s", ""}},         {"inventory", "Inventory", {"i", ""}},
    {"map", "Full map", {"m", ""}},            {"audio", "Audio: full / effects / mute", {"a", ""}},
};

}  // namespace

const char* actionId(Action a) { return kActions[static_cast<size_t>(a)].id; }
const char* actionLabel(Action a) { return kActions[static_cast<size_t>(a)].label; }
const std::array<std::string, kKeySlots>& defaultKeys(Action a) { return kActions[static_cast<size_t>(a)].keys; }

const char* soundModeLabel(const Settings& s) {
  if (s.music && s.sfx) return "Full Audio";
  if (s.sfx) return "Effects Only";
  return s.music ? "Music Only" : "Full Mute";
}

void cycleSoundMode(Settings& s) {
  const bool full = s.music && s.sfx, effects = !s.music && s.sfx;
  if (full) { s.music = false; s.sfx = true; }
  else if (effects) { s.music = false; s.sfx = false; }
  else { s.music = true; s.sfx = true; }  // Full Mute, or Music Only: back to everything
}

void Settings::resetKeys() {
  for (int a = 0; a < kActionCount; ++a) keys[static_cast<size_t>(a)] = kActions[a].keys;
}

std::string settingsPath() {
  if (const char* env = std::getenv("LABYTERM_SETTINGS")) return env;
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return (fs::path(xdg) / "labyterm/settings.json").string();
  if (const char* home = std::getenv("HOME")) return (fs::path(home) / ".config/labyterm/settings.json").string();
  return {};
}

void Settings::load() {
  const std::string path = settingsPath();
  std::ifstream in(path);
  if (!in) return;
  const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (!j.is_object()) return;
  const auto num = [&](const char* k, int& v, int lo, int hi) {
    if (j.contains(k) && j[k].is_number_integer()) v = std::clamp(j[k].get<int>(), lo, hi);
  };
  const auto flag = [&](const char* k, bool& v) {
    if (j.contains(k) && j[k].is_boolean()) v = j[k].get<bool>();
  };
  flag("music", music);
  flag("sfx", sfx);
  num("master", master, 0, 10);
  num("musicVolume", musicVol, 0, 10);
  num("sfxVolume", sfxVol, 0, 10);
  flag("themeColors", themeColors);
  if (j.contains("playerName") && j["playerName"].is_string()) playerName = j["playerName"].get<std::string>().substr(0, 14);
  if (playerName == "Wanderer") playerName.clear();  // the default of earlier builds, saved by mistake as if it had been typed
  num("crt", crt, -1, 3);
  num("mazeStyle", mazeStyle, 0, 1);
  num("keyRepeatMs", keyRepeat, -1, 2000);
  if (j.contains("keys") && j["keys"].is_object()) {
    // A file from before the Audio key (it had Scrolls on R, Sound on S, or earlier Scrolls on S): keep the defaults for Scrolls (S) and Audio (A).
    const bool oldKeys = !j["keys"].contains("audio");
    for (int a = 0; a < kActionCount; ++a) {
      if (oldKeys && static_cast<Action>(a) == Action::Scrolls) continue;
      const auto it = j["keys"].find(kActions[a].id);
      if (it == j["keys"].end() || !it->is_array()) continue;
      for (size_t s = 0; s < kKeySlots; ++s) keys[static_cast<size_t>(a)][s] = s < it->size() && (*it)[s].is_string() ? (*it)[s].get<std::string>() : "";
    }
  }
}

void Settings::save() const {
  if (!saveEnabled) return;
  const std::string path = settingsPath();
  if (path.empty()) return;
  nlohmann::json j;
  j["music"] = music;
  j["sfx"] = sfx;
  j["master"] = master;
  j["musicVolume"] = musicVol;
  j["sfxVolume"] = sfxVol;
  j["themeColors"] = themeColors;
  j["playerName"] = playerName;
  j["crt"] = crt;
  j["mazeStyle"] = mazeStyle;
  j["keyRepeatMs"] = keyRepeat;
  for (int a = 0; a < kActionCount; ++a) j["keys"][kActions[a].id] = keys[static_cast<size_t>(a)];
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream(path) << j.dump(2) << "\n";
}

Settings& settings() {
  static Settings s;
  return s;
}

}  // namespace laby
