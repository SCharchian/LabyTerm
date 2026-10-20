#pragma once
#include <string>

#include "core/Settings.hpp"
#include "render/Backend.hpp"

namespace laby {

// The name a key goes by in the settings file and on the key-bindings page: "Up" "Down" "Left" "Right" "Enter" "Backspace", or the
// lowercase character ("z", "5", ","). Empty for keys that can't be bound (Escape, unknown keys).
std::string keyName(Key key, char32_t text);
// How it is shown: "Up", "Enter", "Z".
std::string keyDisplay(const std::string& name);

// Turns a raw key press into what the game understands, through the bindings: a key bound to an action becomes that action's own key
// (Forward is Key::Up, Scrolls is the letter s, ...). A key that is some action's default but was unbound by the player does nothing
// (returns false). Everything else (Escape, the combat letter shortcuts, ...) passes unchanged.
bool translateKey(Key key, char32_t text, Key& outKey, char32_t& outText);

// Binds `name` to `slot` of `a`; if another action had that key it loses it (its label is returned, else empty).
std::string bindKey(Action a, int slot, const std::string& name);

}  // namespace laby
