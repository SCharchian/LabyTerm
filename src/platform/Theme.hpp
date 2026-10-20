#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "render/Cell.hpp"

namespace laby {

// Colors taken from the user's current Omarchy theme
// (~/.local/state/omarchy/current/theme/colors.toml), with a green-on-black
// "hacker terminal" fallback if it isn't available or if the user turned theme colors off
// (tuning `display.themeColors` = 0). `display.themeStrength` and `display.themeSaturation` tone the theme down
// (see loadTheme) and `display.frameMix` sets how bright the frame role is.
//
// UI code should prefer the *roles* at the bottom (frame, good, mana, danger, coin, charmee) to the raw palette
// entries: a role says what a color means, and `applyRoles()` is the one place that decides which theme
// color plays it, so the whole look can be retuned there.
struct Theme {
  Rgb bg{0, 17, 11}, fg{230, 246, 240};
  Rgb accent{209, 255, 176}, muted{89, 133, 109}, selection{31, 48, 42};
  Rgb red{255, 51, 112}, green{0, 168, 107}, yellow{209, 255, 176};
  Rgb blue{255, 102, 0}, magenta{143, 200, 92}, cyan{128, 212, 181};
  Rgb ansi[16]{};   // color0..color15
  // Every color the theme file offered, by key (after the tone-down knobs), for the roles to choose from: the standard slots
  // above plus `orange`, `brown`, `bright_*` and `color0`..`color15` where the file has them. Empty for the built-in palette.
  std::vector<std::pair<std::string, Rgb>> pool;
  bool fromOmarchy = false;
  std::string name;

  // Roles, derived from the palette above by applyRoles() (called by loadTheme / builtinTheme). Each role picks a theme color
  // that is legible on the background and fits what the role means (see the role table in Theme.cpp), so a theme whose "red"
  // is unreadable or whose "blue" is orange still gets a sensible health, danger and gold color.
  Rgb frame, good, mana, danger, coin, charmee;
  Rgb lift;           // the elevator in the 3D view
  Rgb surround;       // floor and ceiling in the 3D view: a theme color of another hue than the accent (the muted one when the theme has none), at full strength
  Rgb lair;           // a boss lair in the 3D view: the accent warmed towards danger
  Rgb gauge;          // the fill of the enemy health bar: `danger` when the plain text color reads on it, else another theme color or `danger` with its brightness forced
  Rgb selectFill;     // the solid fill of a selected row / button: `good`, darkened when it is so bright that its glow would drown the label
  Rgb onGood;         // text on that fill
  Rgb better, worse;  // "better" / "worse" numbers in item comparisons
  bool light = false; // a light theme (light background): shading and highlights go the other way
};

// WCAG relative luminance (0 black .. 1 white) and contrast ratio (1 .. 21) of colors.
double luminance(Rgb c);
double contrast(Rgb a, Rgb b);

// Fills in the role colors from the palette (and whatever else the theme file offered, when called from loadTheme).
void applyRoles(Theme& t);

// The built-in green-on-black palette (what you get without an Omarchy theme, or with theme colors turned off).
Theme builtinTheme();

// Linear blend of two colors: t = 0 gives a, t = 1 gives b.
inline Rgb mix(Rgb a, Rgb b, double t) {
  const auto lerp = [t](int x, int y) { return static_cast<std::uint8_t>(x + (y - x) * t + 0.5); };
  return Rgb{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b)};
}

// The Omarchy theme if there is one and `useOmarchy` is set, else the built-in palette. The no-argument form follows the
// tuning knob `display.themeColors`.
Theme loadTheme(bool useOmarchy);
Theme loadTheme();
// The player's own switch (Options): Omarchy colors need both this and the tuning knob. The ThemeWatcher notices a change.
void setUserThemeColors(bool on);
bool userThemeColors();

// Watches the theme files and reloads when Omarchy switches themes. Cheap to call every
// frame: it only stats the files every `intervalMs`.
class ThemeWatcher {
 public:
  explicit ThemeWatcher(unsigned intervalMs = 500) : intervalMs_(intervalMs) {}
  // Returns true (and updates `theme`) if the theme on disk changed since the last call.
  // Also reloads when one of the `display.theme*` / `frameMix` tuning knobs changes.
  bool poll(Theme& theme, std::uint64_t nowMs);

 private:
  double lastKnobs_[4] = {-1, -1, -1, -1};  // display.themeColors / themeStrength / themeSaturation / frameMix as last loaded
  unsigned intervalMs_;
  std::uint64_t lastCheckMs_ = 0;
  long long stamp_ = -1;
  bool primed_ = false;
};

}  // namespace laby
