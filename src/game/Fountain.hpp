#pragma once
#include <array>
#include <functional>
#include <string>

#include "core/Rng.hpp"
#include "game/Hero.hpp"

namespace laby {

// What a drink does (SecretDoor.lua Fountain.FOUNTAIN_*).
enum class FountainEffect { Heal = 1, StatUp, StatDown, Teleport, ShadowFight, Invisibility, Alarm };

// The secret door's fountain (SecretDoor.lua class Fountain). The seven colours are fixed; which effect each one has is shuffled once
// per run, and a colour you have drunk from tells you its effect on later visits.
class Fountain {
 public:
  static constexpr int kColors = 7;
  using Say = std::function<void(const std::string&)>;

  // A new run: shuffles which colour has which effect and forgets what was drunk.
  void newRun(Rng& rng);
  // A floor's fountain is rolled the first time it is offered: a random colour and its effect.
  void roll(Rng& rng) { color_ = static_cast<int>(rng.range(1, kColors)) - 1; }
  void setColor(int c) { color_ = c % kColors; }  // 0-based: lets a floor own a fixed colour
  int color() const { return color_; }  // 0-based
  FountainEffect effect() const { return effects_[static_cast<size_t>(color_)]; }
  bool known() const { return drunk_[static_cast<size_t>(color_)]; }  // this colour has been drunk before, so its effect is told
  void markDrunk() { drunk_[static_cast<size_t>(color_)] = true; }

  // For saving: which effect each colour has and which colours were drunk (the floor's own colour is rolled when offered).
  const std::array<FountainEffect, kColors>& effects() const { return effects_; }
  const std::array<bool, kColors>& drunk() const { return drunk_; }
  void restore(const std::array<FountainEffect, kColors>& e, const std::array<bool, kColors>& d) { effects_ = e; drunk_ = d; }

  static const char* colorName(int color);          // "RED" ...
  static const char* effectText(FountainEffect e);  // "It's healing elixir!" ...

  // The effects that only change the hero (the others need the dungeon or a fight and are done by the game session).
  static void heal(Hero& h, const Say& say);
  static void statChange(Hero& h, Rng& rng, int sign, const Say& say);  // +1: a random stat goes up, -1: down (never below 1)

 private:
  std::array<FountainEffect, kColors> effects_{FountainEffect::Heal, FountainEffect::StatUp, FountainEffect::StatDown, FountainEffect::Teleport,
                                               FountainEffect::ShadowFight, FountainEffect::Invisibility, FountainEffect::Alarm};
  std::array<bool, kColors> drunk_{};
  int color_ = 0;
};

}  // namespace laby
