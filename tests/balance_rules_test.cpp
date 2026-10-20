// The balance rules decided with the author: Charm/Trick/Pray chances, the XP back-off, the class rotation, Boss10, the recovery floor
// and the master's piece.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/Tuning.hpp"
#include "game/Combat.hpp"
#include "game/Hero.hpp"
#include "game/Item.hpp"
#include "game/Monster.hpp"
#include "game/Store.hpp"

using namespace laby;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);           \
      return 1;                                                         \
    }                                                                   \
  } while (0)

static Hero heroAt(int level, double intl, double cha, double vit) {
  Hero h;
  h.level = level;
  h.calc = {10, 10, intl, vit, cha};
  h.maxHP = 100;
  h.hp = 100;
  return h;
}

int main() {
  const auto& a = tuning().heroAverage;
  Rng rng(7);
  Monster m = Monster::create(rng, 1, 5, false);
  m.level = 5;

  // an average hero of the level, facing a monster of the level: Charm 50%, Trick 33%
  Hero avg = heroAt(5, a.intBase + 4 * a.intPerLevel, a.chaBase + 4 * a.chaPerLevel, a.vitBase + 4 * a.vitPerLevel);
  CHECK(std::fabs(Combat::charmChance(avg, m) - tuning().charm.baseChance) < 1e-9);
  CHECK(std::fabs(Combat::trickChance(avg, m) - tuning().trick.baseChance) < 1e-9);
  // 10% more CHA = 10 points more; 10% less = 10 points less; clamped 5%..95%
  Hero rich = avg;
  rich.calc[static_cast<size_t>(Stat::CHA)] *= 1.1;
  CHECK(std::fabs(Combat::charmChance(rich, m) - 0.6) < 1e-9);
  rich.calc[static_cast<size_t>(Stat::CHA)] = 1000;
  CHECK(Combat::charmChance(rich, m) == tuning().charm.maxChance);
  rich.calc[static_cast<size_t>(Stat::CHA)] = 0;
  CHECK(Combat::charmChance(rich, m) == tuning().charm.minChance);
  // the monster's level matters a little
  Monster tough = m;
  tough.level = 9;
  CHECK(Combat::charmChance(avg, tough) < Combat::charmChance(avg, m));

  // Pray: a floor of 5% when comfortable, rises with desperation, never above 50%
  CHECK(std::fabs(Combat::prayChance(avg, m) - tuning().pray.baseChance) < 1e-9);
  Hero hurt = avg;
  hurt.hp = 10;
  CHECK(Combat::prayChance(hurt, m) > Combat::prayChance(avg, m) + 0.2);
  tough.level = 9;
  CHECK(Combat::prayChance(hurt, tough) > Combat::prayChance(hurt, m));
  Hero saint = hurt;
  saint.calc[static_cast<size_t>(Stat::INT)] = 500;
  CHECK(Combat::prayChance(saint, tough) == tuning().pray.maxChance);

  // XP back-off: -8, -16, -24, -50, -75 (5 or more levels above)
  Monster x = m;
  x.experience = 100;
  const double k = tuning().progression.killExpMultiplier;
  const auto exp = [&](double cut) { return static_cast<int>(std::floor(100 * k * (1 - cut) + 1e-9)); };
  Hero h = heroAt(5, 10, 10, 10);
  CHECK(Combat::experienceFor(h, x) == exp(0));
  h.level = 6;
  CHECK(Combat::experienceFor(h, x) == exp(0.08));
  h.level = 7;
  CHECK(Combat::experienceFor(h, x) == exp(0.16));
  h.level = 8;
  CHECK(Combat::experienceFor(h, x) == exp(0.24));
  h.level = 9;
  CHECK(Combat::experienceFor(h, x) == exp(0.50));
  h.level = 20;
  CHECK(Combat::experienceFor(h, x) == exp(0.75));
  h.level = 3;  // a monster above you pays the full amount
  CHECK(Combat::experienceFor(h, x) == exp(0));

  // the class rotation: HUMAN, FAE, UNDEAD, ANIMAL, DEMON (the dominant class wins about 93% of the time)
  const MonsterClass want[5] = {MonsterClass::HUMAN, MonsterClass::FAE, MonsterClass::UNDEAD, MonsterClass::ANIMAL, MonsterClass::DEMON};
  for (int floor : {1, 2, 3, 4, 5, 10, 14}) {
    int hits = 0;
    for (int i = 0; i < 200; ++i) hits += Monster::create(rng, floor, floor, false).cls == want[(floor - 1) % 5];
    CHECK(hits > 160);
  }

  // Boss10: only when asked for (and only a boss), a tougher boss with more XP
  const Monster plain = Monster::create(rng, 10, 10, true, false);
  const Monster ten = Monster::create(rng, 10, 10, true, true);
  CHECK(!plain.isBoss10 && ten.isBoss10 && ten.isBoss);
  CHECK(!Monster::create(rng, 10, 10, false, true).isBoss10);
  double plainLevel = 0, tenLevel = 0;
  for (int i = 0; i < 50; ++i) {
    plainLevel += Monster::create(rng, 10, 10, true, false).level;
    tenLevel += Monster::create(rng, 10, 10, true, true).level;
  }
  CHECK(tenLevel / 50 - plainLevel / 50 == tuning().monsters.boss10LevelBonus);

  // the recovery floor (11): the monster keeps floor 11's level but its stats are softer than a floor-11 monster's
  double recov = 0, normal = 0;
  for (int i = 0; i < 300; ++i) {
    const Monster r = Monster::create(rng, 11, 11, false);
    CHECK(r.level == 11);
    for (double s : r.stats) recov += s;
    for (double s : Monster::create(rng, 12, 11, false).stats) normal += s;  // (floor 12 is not a recovery floor)
  }
  CHECK(recov < normal * 0.97);

  // masterwork: always prefixed, never below the hero's level, the Blacksmith's first slot
  for (int i = 0; i < 100; ++i) {
    const Item it = masterPiece(rng, 10);
    CHECK(it.masterwork && !it.prefix.empty() && it.level >= 8);
  }
  const auto shop = restockBlacksmith(rng, 10);
  CHECK(shop.size() == 7 && shop[0].masterwork && !shop[1].masterwork && shop[6].type == EquipmentType::Scroll);
  const Treasure t = generateTreasure(rng, 12, true, false, false, true);
  CHECK(t.item.masterwork && !t.item.prefix.empty());

  // One monster hit never takes more than combat.maxHitPercent (boss: maxHitPercentBoss) of the hero's max HP, however strong the monster is
  for (int boss = 0; boss <= 1; ++boss) {
    const int pct = boss ? tuning().combat.maxHitPercentBoss : tuning().combat.maxHitPercent;
    int worst = 0;
    for (int seed = 1; seed <= 60; ++seed) {
      Rng r2(seed);
      Hero hero = heroAt(5, 10, 10, 10);
      hero.maxHP = hero.hp = 40;
      Monster brute = Monster::create(r2, 5, 5, boss != 0);
      brute.stats = {1000, 1000, 10, 10, 10};
      brute.hp = brute.maxHP = 100000;
      Combat c(hero, brute, r2, [&](const std::string&) {});
      c.act(CombatAction::Attack);
      worst = std::max(worst, 40 - hero.hp);
    }
    CHECK(pct > 0 && worst > 0 && worst <= static_cast<int>(std::ceil(40 * pct / 100.0)));
  }
  // proportional armor: a blow as strong as the defense is halved (a stronger monster is never worth less than a weaker one)
  if (tuning().defense.proportional > 0) {
    Rng r3(3);
    int prev = 0;
    for (double str : {10.0, 20.0, 40.0, 80.0}) {
      Hero hero = heroAt(5, 10, 10, 20);
      hero.ac = 20;
      hero.maxHP = hero.hp = 100000;
      Monster mon = Monster::create(r3, 5, 5, false);
      mon.stats = {str, 1000, 10, 10, 10};
      mon.hp = mon.maxHP = 100000;
      mon.ac = 100000;
      int total = 0;
      for (int i = 0; i < 200; ++i) {
        const int before = hero.hp;
        Combat c(hero, mon, r3, [&](const std::string&) {});
        c.act(CombatAction::Attack);
        total += before - hero.hp;
      }
      CHECK(total >= prev);
      prev = total;
    }
  }

  std::printf("balance rules ok\n");
  return 0;
}
