#include "game/Fountain.hpp"

#include <algorithm>
#include <cmath>

namespace laby {

void Fountain::newRun(Rng& rng) {
  effects_ = {FountainEffect::Heal, FountainEffect::StatUp, FountainEffect::StatDown, FountainEffect::Teleport,
              FountainEffect::ShadowFight, FountainEffect::Invisibility, FountainEffect::Alarm};
  drunk_.fill(false);
  color_ = 0;
  for (int i = kColors; i >= 2; --i) std::swap(effects_[static_cast<size_t>(i) - 1], effects_[static_cast<size_t>(rng.range(1, i)) - 1]);  // ShuffleFountainEffects
}

const char* Fountain::colorName(int color) {
  static const char* kNames[kColors] = {"RED", "GREEN", "BLUE", "YELLOW", "PURPLE", "ORANGE", "CLEAR"};
  return kNames[std::clamp(color, 0, kColors - 1)];
}

const char* Fountain::effectText(FountainEffect e) {
  switch (e) {
    case FountainEffect::Heal: return "It's healing elixir!";
    case FountainEffect::StatUp: return "It's empowering waters!";
    case FountainEffect::StatDown: return "It's a tainted spring!";
    case FountainEffect::Teleport: return "It's a teleport trap!";
    case FountainEffect::ShadowFight: return "It's a shadowy reflection!";
    case FountainEffect::Invisibility: return "It's invisibility elixir!";
    case FountainEffect::Alarm: return "It's an alarm trap!";
  }
  return "";
}

// Fountain:DoHeal: at full health and mana the fountain makes you a little tougher instead (Vitality or Intelligence, whichever is lower).
void Fountain::heal(Hero& h, const Say& say) {
  if (h.hp >= h.maxHP && h.mp >= h.maxMP) {
    const bool vit = h.stat(Stat::VIT) <= h.stat(Stat::INT);
    h.base[static_cast<size_t>(vit ? Stat::VIT : Stat::INT)] += 1;
    h.recalc();
    say(vit ? "You feel refreshed! Vitality +1!" : "You feel refreshed! Intelligence +1!");
  } else {
    say("You feel refreshed!");
    say("All HP & MP restored!");
  }
  h.hp = h.maxHP;
  h.mp = h.maxMP;
}

// Fountain:DoStatUp / DoStatDown: one random stat, by 1 plus a quarter of your level.
void Fountain::statChange(Hero& h, Rng& rng, int sign, const Say& say) {
  const size_t k = static_cast<size_t>(rng.range(1, 5)) - 1;
  const int mag = static_cast<int>(std::floor(1 + h.level * 0.25));
  double& b = h.base[k];
  const double before = b;
  b = std::max(1.0, b + sign * mag);
  h.recalc();
  const int moved = static_cast<int>(std::abs(b - before));  // a stat stops at 1: say what really changed
  const std::string lead = sign > 0 ? "You feel energized... " : "You feel withered... ";
  if (moved == 0) say(lead + std::string(statLongName(k)) + " cannot go any " + (sign > 0 ? "higher" : "lower") + "!");
  else say(lead + statLongName(k) + (sign > 0 ? " +" : " -") + std::to_string(moved) + "!");
}

}  // namespace laby
