#include "ui/KeyMap.hpp"

#include <cctype>

namespace laby {

std::string keyName(Key key, char32_t text) {
  switch (key) {
    case Key::Up: return "Up";
    case Key::Down: return "Down";
    case Key::Left: return "Left";
    case Key::Right: return "Right";
    case Key::Enter: return "Enter";
    case Key::Backspace: return "Backspace";
    case Key::Escape: return "";
    default: break;
  }
  if (text == U' ') return "Space";
  if (text > 32 && text < 127) return std::string(1, static_cast<char>(std::tolower(static_cast<int>(text))));
  return "";
}

std::string keyDisplay(const std::string& name) {
  if (name.empty()) return "--";
  if (name.size() == 1) return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(name[0]))));
  return name;
}

bool translateKey(Key key, char32_t text, Key& outKey, char32_t& outText) {
  outKey = key;
  outText = text;
  const std::string name = keyName(key, text);
  if (name.empty()) return true;
  const Settings& s = settings();
  for (int a = 0; a < kActionCount; ++a)
    for (const std::string& k : s.keys[static_cast<size_t>(a)])
      if (k == name) {
        outText = 0;
        switch (static_cast<Action>(a)) {
          case Action::Forward: outKey = Key::Up; break;
          case Action::Back: outKey = Key::Down; break;
          case Action::TurnLeft: outKey = Key::Left; break;
          case Action::TurnRight: outKey = Key::Right; break;
          case Action::Confirm: outKey = Key::Enter; break;
          case Action::Cancel: outKey = Key::B; break;
          case Action::Scrolls: outKey = Key::Other; outText = U's'; break;
          case Action::Inventory: outKey = Key::Other; outText = U'i'; break;
          case Action::Map: outKey = Key::Other; outText = U'm'; break;
          case Action::Audio: outKey = Key::Other; outText = U'a'; break;
          default: break;
        }
        return true;
      }
  for (int a = 0; a < kActionCount; ++a)
    for (const std::string& k : defaultKeys(static_cast<Action>(a)))
      if (k == name) return false;  // the player moved it away from this action
  return true;
}

std::string bindKey(Action a, int slot, const std::string& name) {
  Settings& s = settings();
  std::string taken;
  for (int o = 0; o < kActionCount; ++o)
    for (std::string& k : s.keys[static_cast<size_t>(o)])
      if (k == name && !(o == static_cast<int>(a))) {
        k.clear();
        taken = actionLabel(static_cast<Action>(o));
      }
  auto& mine = s.keys[static_cast<size_t>(a)];
  for (std::string& k : mine)
    if (k == name) k.clear();  // no key twice on one action
  mine[static_cast<size_t>(slot)] = name;
  return taken;
}

}  // namespace laby
