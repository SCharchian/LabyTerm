#include "platform/Theme.hpp"

#include "core/Tuning.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace laby {
namespace {

bool parseHex(const std::string& s, Rgb& out) {
  if (s.size() != 7 || s[0] != '#') return false;
  char* end = nullptr;
  unsigned long v = std::strtoul(s.c_str() + 1, &end, 16);
  if (*end) return false;
  out = {static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8),
         static_cast<std::uint8_t>(v)};
  return true;
}

std::string trim(std::string s) {
  auto a = s.find_first_not_of(" \t\r\n");
  auto b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

namespace fs = std::filesystem;

// $LABYTERM_THEME_DIR (a dir containing colors.toml + theme.name) overrides the Omarchy
// location; handy for testing without switching the desktop theme. Without an Omarchy theme, the player's own
// folder (~/.config/labyterm/theme, which tools/ghostty_theme.sh writes) is used, so no variable is needed.
fs::path themeDir() {
  if (const char* env = std::getenv("LABYTERM_THEME_DIR")) return env;
  const char* home = std::getenv("HOME");
  if (!home) return {};
  const fs::path omarchy = fs::path(home) / ".local/state/omarchy/current/theme";
  std::error_code ec;
  if (fs::exists(omarchy / "colors.toml", ec)) return omarchy;
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  return (xdg && *xdg ? fs::path(xdg) : fs::path(home) / ".config") / "labyterm/theme";
}

// theme.name sits beside the theme dir in Omarchy (current/theme.name); allow it inside too.
fs::path themeNameFile(const fs::path& dir) {
  std::error_code ec;
  if (fs::exists(dir / "theme.name", ec)) return dir / "theme.name";
  return dir.parent_path() / "theme.name";
}

}  // namespace

namespace {

// One theme color after the user's tone-down knobs: first its saturation is scaled about its own brightness, then the
// result is blended with the built-in palette's color for the same slot.
Rgb tone(Rgb themed, Rgb builtin, double strength, double saturation) {
  const double luma = 0.299 * themed.r + 0.587 * themed.g + 0.114 * themed.b;
  const auto sat = [&](int v) { return std::clamp(luma + (v - luma) * saturation, 0.0, 255.0); };
  const Rgb s{static_cast<std::uint8_t>(sat(themed.r) + 0.5), static_cast<std::uint8_t>(sat(themed.g) + 0.5), static_cast<std::uint8_t>(sat(themed.b) + 0.5)};
  return mix(builtin, s, std::clamp(strength, 0.0, 1.0));
}

}  // namespace

double luminance(Rgb c) {
  const auto lin = [](int v) {
    const double x = v / 255.0;
    return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

double contrast(Rgb a, Rgb b) {
  const double la = luminance(a), lb = luminance(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

namespace {

// ---- Role table -------------------------------------------------------------------------------------------------------
// A role says what a color MEANS in the game; the theme says what colors exist. Each role lists the theme colors that
// usually play it, in order, the hue it should have (so "danger" is reddish even if the theme's `red` slot is something
// else), and how much contrast it needs against the background. A theme color is accepted when it is
//   1. legible: its contrast with the background is at least `minContrast`;
//   2. on-hue: the color is chromatic and within `range` degrees of `hue` (skipped when the role has no hue);
//   3. distinct from the roles listed in `avoid` (so the elevator is not the same color as the gold).
// The search, per role:
//   a. the role's own slots in order (red, color1, bright_red, color9 ...) that pass all three tests;
//   b. any other color the theme has, nearest to the role's hue first, that passes all three;
//   c. the role's own slots again, ignoring the hue test (a monochrome theme has no hues to match);
//   d. the first slot, pushed towards the foreground until it is legible.
// So a good theme keeps its own colors, a theme with an unreadable or odd slot borrows another of its colors, and nothing
// ever ends up unreadable.
struct RoleSpec {
  std::vector<const char*> slots;
  double hue;       // degrees, the role's natural hue; < 0: any
  double range;     // how far off that hue still counts
  double minContrast;
};

struct Hsv { double h, s, v; };
Hsv toHsv(Rgb c) {
  const double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
  const double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
  double h = 0;
  if (d > 0) {
    if (mx == r) h = std::fmod((g - b) / d, 6.0);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h *= 60;
    if (h < 0) h += 360;
  }
  return {h, mx > 0 ? d / mx : 0, mx};
}
double hueDistance(double a, double b) {
  const double d = std::fabs(a - b);
  return std::min(d, 360 - d);
}
bool chromatic(const Hsv& c) { return c.s >= 0.25 && c.v >= 0.25; }
int rgbDistance(Rgb a, Rgb b) { return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b); }

// Moves `c` towards `towards` just far enough to reach `minContrast` against `bg` (as far as it goes if it never does).
Rgb legible(Rgb c, Rgb bg, Rgb towards, double minContrast) {
  if (contrast(c, bg) >= minContrast) return c;
  Rgb out = c;
  for (int i = 1; i <= 20; ++i) {
    out = mix(c, towards, i / 20.0);
    if (contrast(out, bg) >= minContrast) break;
  }
  return out;
}

class Palette {
 public:
  explicit Palette(const Theme& t) {
    add("accent", t.accent); add("red", t.red); add("green", t.green); add("yellow", t.yellow);
    add("blue", t.blue); add("magenta", t.magenta); add("cyan", t.cyan);
    for (const auto& [k, v] : t.pool) add(k, v);
  }
  const Rgb* find(const std::string& k) const {
    for (const auto& e : colors_) if (e.first == k) return &e.second;
    return nullptr;
  }
  const std::vector<std::pair<std::string, Rgb>>& all() const { return colors_; }

 private:
  void add(const std::string& k, Rgb c) { if (!find(k)) colors_.emplace_back(k, c); }
  std::vector<std::pair<std::string, Rgb>> colors_;
};

Rgb resolve(const RoleSpec& spec, const Palette& pal, Rgb bg, Rgb fg, const std::vector<Rgb>& avoid = {}) {
  const auto distinct = [&](Rgb c) {
    for (const Rgb& a : avoid) if (rgbDistance(c, a) < 90) return false;
    return true;
  };
  const auto legibleOn = [&](Rgb c) { return contrast(c, bg) >= spec.minContrast; };
  const auto onHue = [&](Rgb c) {
    if (spec.hue < 0) return true;
    const Hsv h = toHsv(c);
    return chromatic(h) && hueDistance(h.h, spec.hue) <= spec.range;
  };
  // a. the role's own slots
  for (const char* s : spec.slots)
    if (const Rgb* c = pal.find(s); c && legibleOn(*c) && onHue(*c) && distinct(*c)) return *c;
  // a2. an on-hue slot that is only a little too faint: the same color, darkened / lightened just enough, rather than another hue
  if (spec.hue >= 0)
    for (const char* s : spec.slots)
      if (const Rgb* c = pal.find(s); c && onHue(*c) && contrast(*c, bg) >= 0.6 * spec.minContrast) {
        const Rgb fixed = legible(*c, bg, fg, spec.minContrast);
        if (distinct(fixed) && onHue(fixed)) return fixed;
      }
  // b. anything else the theme has, nearest the role's hue first
  if (spec.hue >= 0) {
    std::vector<std::pair<double, Rgb>> others;
    for (const auto& [k, c] : pal.all()) {
      const Hsv h = toHsv(c);
      if (chromatic(h)) others.emplace_back(hueDistance(h.h, spec.hue), c);
    }
    std::stable_sort(others.begin(), others.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    for (const auto& [d, c] : others)
      if (legibleOn(c) && onHue(c) && distinct(c)) return c;
  }
  // c. the role's own slots, whatever their hue
  for (const char* s : spec.slots)
    if (const Rgb* c = pal.find(s); c && legibleOn(*c) && distinct(*c)) return *c;
  // d. make the first slot readable
  for (const char* s : spec.slots)
    if (const Rgb* c = pal.find(s)) return legible(*c, bg, fg, spec.minContrast);
  return legible(fg, bg, fg, spec.minContrast);
}

}  // namespace

void applyRoles(Theme& t) {
  t.light = luminance(t.bg) > 0.4;
  // The base colors first: text and the accent must be readable on the background, whatever the theme says.
  const Rgb ends = t.light ? Rgb{0, 0, 0} : Rgb{255, 255, 255};  // the extreme that always contrasts with the background
  t.fg = legible(t.fg, t.bg, ends, 7.0);
  t.accent = legible(t.accent, t.bg, t.fg, 3.0);
  t.muted = legible(t.muted, t.bg, t.fg, 3.0);
  if (contrast(t.selection, t.bg) < 1.15) t.selection = mix(t.bg, t.fg, 0.14);

  const Palette pal(t);
  const double kRed = 355, kGreen = 135, kCyan = 195, kGold = 45;
  t.danger = resolve({{"red", "color1", "bright_red", "color9"}, kRed, 40, 3.0}, pal, t.bg, t.fg);
  t.good = resolve({{"green", "color2", "bright_green", "color10"}, kGreen, 55, 3.0}, pal, t.bg, t.fg);
  t.mana = resolve({{"cyan", "color6", "bright_cyan", "color14", "blue", "color4", "bright_blue", "color12"}, kCyan, 45, 3.0}, pal, t.bg, t.fg);
  t.coin = resolve({{"yellow", "color3", "bright_yellow", "color11", "orange", "brown"}, kGold, 30, 3.0}, pal, t.bg, t.fg);
  t.lift = resolve({{"blue", "color4", "bright_blue", "color12", "magenta", "color5", "bright_magenta", "color13"}, -1, 0, 3.0}, pal, t.bg, t.fg,
                   {t.danger, t.good, t.mana, t.coin});
  // charmees: between magenta and blue, apart from good / danger / mana
  const Rgb mag = pal.find("magenta") ? *pal.find("magenta") : t.magenta, blu = pal.find("blue") ? *pal.find("blue") : t.blue;
  t.charmee = legible(mix(mag, blu, 0.5), t.bg, t.fg, 3.0);
  t.lair = legible(mix(t.accent, t.danger, 0.4), t.bg, t.fg, 3.0);
  // Floor and ceiling: a theme color near the accent's hue (25..100 degrees away, about 60 preferred), because a far hue (orange walls on a
  // teal floor) clashes; a theme with none gives the muted color. It is then desaturated and pulled towards the background, so it reads as
  // a shadowy backdrop that the walls stand out from.
  {
    const double ah = toHsv(t.accent).h;
    Rgb pick = t.muted;
    double best = 1e9;
    for (const auto& [k, c] : pal.all()) {
      const Hsv h = toHsv(c);
      if (!chromatic(h) || h.s < 0.25) continue;
      const double d = hueDistance(h.h, ah);
      if (d < 25 || d > 100) continue;
      if (std::abs(d - 60) < best) { best = std::abs(d - 60); pick = c; }
    }
    const double grey = 0.299 * pick.r + 0.587 * pick.g + 0.114 * pick.b;
    pick = mix(pick, Rgb{static_cast<std::uint8_t>(grey), static_cast<std::uint8_t>(grey), static_cast<std::uint8_t>(grey)}, 0.4);
    t.surround = mix(t.bg, pick, 0.90);
  }

  // Panel outlines: the accent softened, but never so soft that it vanishes into the background.
  const double frameMix = std::clamp(tuning().display.frameMix, 0.0, 1.0);
  t.frame = mix(t.bg, t.accent, frameMix);
  for (double m = frameMix; contrast(t.frame, t.bg) < 1.8 && m < 1.0;) { m = std::min(1.0, m + 0.05); t.frame = mix(t.bg, t.accent, m); }

  // Text on the `good`-filled selection highlight: the accent as before when it reads on the fill, else the plain text color, else
  // whichever of the background and the extremes reads best.
  // A very bright fill blooms into its own label in the CRT filter (a dark label on pure green is washed out), so on a dark theme it is
  // darkened until it is only moderately bright, and the label is then the bright text color.
  t.selectFill = t.good;
  const bool darkened = !t.light && luminance(t.good) > 0.45;
  if (darkened)
    for (int i = 1; i <= 20 && luminance(t.selectFill) > 0.2; ++i) t.selectFill = mix(t.good, t.bg, i * 0.05);
  const double need = darkened ? 4.5 : 3.0;
  t.onGood = t.accent;
  if (contrast(t.accent, t.selectFill) < need) {
    t.onGood = t.fg;
    for (const Rgb c : {Rgb{255, 255, 255}, t.bg, Rgb{0, 0, 0}})
      if (contrast(t.onGood, t.selectFill) < need && contrast(c, t.selectFill) > contrast(t.onGood, t.selectFill)) t.onGood = c;
  }

  // The enemy health bar's fill. Its label is the plain text color, so the fill must contrast with that (and with the background,
  // or the bar would vanish). `danger` when it does; else a theme color within 60 degrees of danger's hue that does (nearest hue, then brightest);
  // else, when no color of the theme can (a theme that is all one bright hue), the danger-like color that needs the least change,
  // moved towards black (or white, on a light theme) just far enough.
  {
    constexpr double kLabelNeed = 4.5, kBarNeed = 2.5, kNearHue = 60;  // 4.5 like the selection fill: the CRT glow eats into contrast
    const auto fits = [&](Rgb c) { return contrast(c, t.fg) >= kLabelNeed && contrast(c, t.bg) >= kBarNeed; };
    t.gauge = t.danger;
    if (!fits(t.danger)) {
      const Hsv dh = toHsv(t.danger);
      bool found = false;
      std::pair<int, double> bestKey{0, 0};
      for (const auto& [k, c] : pal.all()) {
        const Hsv h = toHsv(c);
        if (!chromatic(h) || hueDistance(h.h, dh.h) > kNearHue || !fits(c)) continue;
        const std::pair<int, double> key{static_cast<int>(hueDistance(h.h, dh.h) / 30), -luminance(c)};
        if (!found || key < bestKey) { found = true; bestKey = key; t.gauge = c; }
      }
      if (!found) {
        std::vector<Rgb> cands{t.danger};
        for (const auto& [k, c] : pal.all()) if (chromatic(toHsv(c)) && hueDistance(toHsv(c).h, dh.h) <= kNearHue) cands.push_back(c);
        int bestMove = 1 << 30;
        for (const Rgb c : cands) {
          const Rgb towards = luminance(t.fg) > luminance(c) ? Rgb{0, 0, 0} : Rgb{255, 255, 255};
          Rgb out = c;
          for (int i = 1; i <= 50 && !fits(out); ++i) out = mix(c, towards, i / 50.0);
          if (!fits(out)) continue;
          if (const int move = rgbDistance(c, out); move < bestMove) { bestMove = move; t.gauge = out; }
        }
      }
    }
  }

  // "Better" / "worse" in item comparisons: the theme's good / danger when they stand clear of the plain text, else a vivid
  // green / red (darkened on a light theme so they can be read).
  const Rgb vividGreen{40, 255, 70}, vividRed{255, 60, 70};
  // The theme's own color only counts when it really is green / red: some themes have no red at all, and `danger` then falls back
  // to a green, which would show a worse number in green.
  const auto hueNear = [](Rgb c, double hue) { const Hsv h = toHsv(c); return chromatic(h) && hueDistance(h.h, hue) <= 45; };
  t.better = rgbDistance(t.good, t.fg) >= 150 && hueNear(t.good, kGreen) ? t.good : legible(vividGreen, t.bg, Rgb{0, 90, 20}, 3.0);
  t.worse = rgbDistance(t.danger, t.fg) >= 150 && hueNear(t.danger, kRed) ? t.danger : legible(vividRed, t.bg, Rgb{110, 0, 15}, 3.0);
}

Theme builtinTheme() {
  Theme t;
  applyRoles(t);
  return t;
}

namespace {
bool gUserThemeColors = true;
}
void setUserThemeColors(bool on) { gUserThemeColors = on; }
bool userThemeColors() { return gUserThemeColors; }

Theme loadTheme() { return loadTheme(tuning().display.themeColors != 0 && gUserThemeColors); }

Theme loadTheme(bool useOmarchy) {
  Theme t = builtinTheme();
  if (!useOmarchy) return t;
  fs::path dir = themeDir();
  if (dir.empty()) return t;
  std::ifstream f(dir / "colors.toml");
  if (!f) return t;

  // colors.toml is flat `key = "value"` lines.
  std::map<std::string, Rgb> c;
  std::string line;
  while (std::getline(f, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = trim(line.substr(0, eq)), val = trim(line.substr(eq + 1));
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
    Rgb rgb;
    if (parseHex(val, rgb)) c[key] = rgb;
  }
  // The fallback chain for each palette slot: the standard key, then the same color under another name (a theme that only
  // defines the 16 ANSI colors still has a red: color1), then the built-in color.
  const auto first = [&](std::initializer_list<const char*> keys, Rgb& dst) {
    for (const char* k : keys)
      if (auto it = c.find(k); it != c.end()) { dst = it->second; return true; }
    return false;
  };
  first({"background"}, t.bg);
  first({"foreground", "color15", "color7"}, t.fg);
  first({"accent", "color4", "color12"}, t.accent);
  if (!first({"muted", "color8"}, t.muted)) t.muted = mix(t.bg, t.fg, 0.45);
  if (!first({"selection", "lighter_background"}, t.selection)) t.selection = mix(t.bg, t.fg, 0.14);
  first({"red", "color1", "bright_red", "color9"}, t.red);
  first({"green", "color2", "bright_green", "color10"}, t.green);
  first({"yellow", "color3", "bright_yellow", "color11"}, t.yellow);
  first({"blue", "color4", "bright_blue", "color12"}, t.blue);
  first({"magenta", "color5", "bright_magenta", "color13"}, t.magenta);
  first({"cyan", "color6", "bright_cyan", "color14"}, t.cyan);
  for (int i = 0; i < 16; ++i) first({("color" + std::to_string(i)).c_str()}, t.ansi[i]);

  // The extra colors the roles may borrow when a slot is a poor match.
  for (const char* k : {"orange", "brown", "bright_red", "bright_green", "bright_yellow", "bright_blue", "bright_magenta", "bright_cyan",
                        "color1", "color2", "color3", "color4", "color5", "color6", "color9", "color10", "color11", "color12", "color13", "color14"})
    if (auto it = c.find(k); it != c.end()) t.pool.emplace_back(k, it->second);

  const Tuning::Display& d = tuning().display;
  if (d.themeStrength != 1.0 || d.themeSaturation != 1.0) {
    const Theme base = builtinTheme();
    for (Rgb Theme::* m : {&Theme::bg, &Theme::fg, &Theme::accent, &Theme::muted, &Theme::selection, &Theme::red, &Theme::green, &Theme::yellow,
                           &Theme::blue, &Theme::magenta, &Theme::cyan})
      t.*m = tone(t.*m, base.*m, d.themeStrength, d.themeSaturation);
    for (int i = 0; i < 16; ++i) t.ansi[i] = tone(t.ansi[i], base.ansi[i], d.themeStrength, d.themeSaturation);
    // the pool's extras blend towards the built-in color of the same hue family
    const auto familyBase = [&](const std::string& k) -> Rgb {
      if (k.find("red") != std::string::npos || k == "color1" || k == "color9") return base.red;
      if (k.find("green") != std::string::npos || k == "color2" || k == "color10") return base.green;
      if (k.find("blue") != std::string::npos || k == "color4" || k == "color12") return base.blue;
      if (k.find("magenta") != std::string::npos || k == "color5" || k == "color13") return base.magenta;
      if (k.find("cyan") != std::string::npos || k == "color6" || k == "color14") return base.cyan;
      return base.yellow;  // yellow, orange, brown
    };
    for (auto& [k, v] : t.pool) v = tone(v, familyBase(k), d.themeStrength, d.themeSaturation);
  }
  t.fromOmarchy = true;
  applyRoles(t);

  std::ifstream n(themeNameFile(dir));
  if (n) std::getline(n, t.name);
  t.name = trim(t.name);
  return t;
}

}  // namespace laby

namespace laby {

bool ThemeWatcher::poll(Theme& theme, std::uint64_t nowMs) {
  if (primed_ && nowMs - lastCheckMs_ < intervalMs_) return false;
  lastCheckMs_ = nowMs;

  const Tuning::Display& d = tuning().display;
  const double knobs[4] = {static_cast<double>(d.themeColors != 0 && gUserThemeColors), d.themeStrength, d.themeSaturation, d.frameMix};
  bool knobsChanged = false;
  for (int i = 0; i < 4; ++i) knobsChanged |= knobs[i] != lastKnobs_[i];
  const bool firstCall = lastKnobs_[0] < 0;
  std::copy(knobs, knobs + 4, lastKnobs_);
  if (knobsChanged && !firstCall) {  // the user edited a color knob while running
    theme = loadTheme();
    return true;
  }

  // Omarchy rewrites colors.toml (and may swap the whole theme dir) on a theme change, so
  // the file's mtime plus the theme name's mtime is a reliable change signal.
  fs::path dir = themeDir();
  std::error_code ec;
  long long stamp = 0;
  for (const fs::path& p : {dir / "colors.toml", themeNameFile(dir)}) {
    auto t = fs::last_write_time(p, ec);
    if (!ec) stamp += t.time_since_epoch().count();
    ec.clear();
  }
  if (!primed_) { primed_ = true; stamp_ = stamp; return false; }
  if (stamp == stamp_) return false;
  stamp_ = stamp;
  theme = loadTheme();
  return true;
}

}  // namespace laby
