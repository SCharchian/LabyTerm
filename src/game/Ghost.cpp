#include "game/Ghost.hpp"

#include <cmath>
#include <numeric>

#include "core/TuningParams.hpp"
#include "core/Tuning.hpp"

namespace laby {

int Ghost::rate(const Item& item) {
  return std::accumulate(item.bonuses.begin(), item.bonuses.end(), 0) + item.level + item.ac + item.hp + item.mp;
}

Item Ghost::pickLoot(const Hero& hero, Rng& rng) {
  const Item* best = nullptr;
  int bestRating = 0;
  for (const auto& slot : hero.equipment) {
    if (!slot) continue;
    const int r = rate(*slot);
    if (!best || r > bestRating) {
      best = &*slot;
      bestRating = r;
    }
  }
  if (best) return *best;
  return randomItem(rng, static_cast<EquipmentType>(rng.range(0, 7)), hero.level + 2);
}

Ghost Ghost::fromHero(const Hero& hero, int floor, Rng& rng) {
  Ghost g;
  g.name = hero.name;
  g.floor = floor;
  g.level = hero.level;
  g.gold = hero.gold;
  g.ac = hero.ac;
  g.maxHP = hero.maxHP;
  g.base = hero.base;
  const int div = std::max(1, tuning().encounters.corpseExpDivisor);
  g.experience = (hero.experience + 1 + div - 1) / div;  // ceil((xp + 1) / div): worth at least 1
  g.loot = pickLoot(hero, rng);
  return g;
}

Monster Ghost::monster() const {
  Monster m;
  m.displayName = "Spectral " + name;
  m.cls = MonsterClass::UNDEAD;
  m.image = "images/Monsters/PlayerCorpse";
  m.stats = base;
  m.level = level;
  m.ac = ac;
  m.maxHP = m.hp = std::max(1, maxHP);
  m.experience = std::max(1, experience);
  return m;
}

Treasure Ghost::treasure() const {
  Treasure t;
  t.gold = gold;
  t.item = loot;
  t.level = level;
  return t;
}

}  // namespace laby
