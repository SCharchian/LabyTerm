#pragma once
#include <string>

#include "core/Rng.hpp"
#include "game/Hero.hpp"
#include "game/Item.hpp"
#include "game/Monster.hpp"

namespace laby {

// A dead hero come back as a monster in a later run (Monster.lua SavePlayerCorpse / Monster:init, "Spectral <name>"). It keeps what the
// original kept: name, the floor and level they died at, gold, AC, max HP, base stats (no gear bonuses, which makes it easier) and
// ONE item, the best-rated one they wore. Beating it pays out that item and the gold.
struct Ghost {
  std::string name;
  int floor = 1, level = 1, gold = 0, ac = 0, maxHP = 1, experience = 1;
  StatTable base{};
  Item loot;

  // The original's rating: every bonus + level + AC + HP + MP.
  static int rate(const Item& item);
  // The worn item with the highest rating (the first one wins a tie). Nothing worn: a random new piece of equipment, hero level + 2.
  static Item pickLoot(const Hero& hero, Rng& rng);
  // Built from a hero who just died on `floor`.
  static Ghost fromHero(const Hero& hero, int floor, Rng& rng);

  // The monster to fight: Undead, full health, "Spectral <name>", its own art.
  Monster monster() const;
  // What beating it pays.
  Treasure treasure() const;
};

}  // namespace laby
