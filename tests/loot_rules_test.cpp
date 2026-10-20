// Loot rules that parity with the original does not cover.
#include <cstdio>

#include "core/Rng.hpp"
#include "core/Tuning.hpp"
#include "game/Hero.hpp"
#include "game/Item.hpp"

using namespace laby;

static int failures = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); ++failures; } } while (0)

int main() {
  // The tenth-floor boss always pays a masterwork: the "you are hurt" / "low on mana" potion roll must not replace it.
  int potionRolls = 0;
  for (int seed = 1; seed <= 2000; ++seed) {
    Rng rng(seed);
    const Treasure t = generateTreasure(rng, 10, false, /*hurt*/ true, /*lowMana*/ true, /*boss10*/ true);
    CHECK(t.item.masterwork, "a boss10 treasure is a masterwork even for a hurt hero");
    potionRolls += t.item.type == EquipmentType::Health_Potion;
  }
  CHECK(potionRolls == 0, "no potion replaces the boss10 masterwork");
  // ... while an ordinary kill still gives the hurt hero a potion now and then
  int potions = 0;
  for (int seed = 1; seed <= 2000; ++seed) {
    Rng rng(seed);
    potions += generateTreasure(rng, 10, false, true, false, false).item.type == EquipmentType::Health_Potion;
  }
  CHECK(potions > 200 && potions < 600, "a hurt hero gets a potion about one time in five");
  // A new run (Hero::roll) and the character screen's Re-Roll (rollStats + rollStartingItem) make the same hero from the same seed,
  // draw the same random numbers, and a new run also wipes what the last hero left behind.
  for (int seed = 1; seed <= 300; ++seed) {
    Rng ra(seed), rb(seed);
    Hero a;
    a.gold = 999; a.score = 77; a.steps = 55; a.level = 9; a.experience = 1234;  // the previous hero's leftovers
    a.charmees.push_back(Monster{});
    Item junk;
    junk.name = junk.displayName = "Junk";
    junk.type = EquipmentType::Health_Potion;
    a.addScroll(junk);
    a.roll(ra);
    Hero b;
    b.rollStats(rb);
    b.rollStartingItem(rb);
    CHECK(a.base == b.base, "same stats");
    CHECK(a.maxHP == b.maxHP && a.hp == b.hp && a.maxMP == b.maxMP && a.mp == b.mp && a.ac == b.ac, "same health, mana and armor");
    CHECK(a.startingItem && b.startingItem && a.startingItem->displayName == b.startingItem->displayName, "same starting item");
    CHECK(a.scrolls.size() == b.scrolls.size(), "same scrolls");
    for (size_t i = 0; i < a.equipment.size(); ++i)
      CHECK(a.equipment[i].has_value() == b.equipment[i].has_value() && (!a.equipment[i] || a.equipment[i]->displayName == b.equipment[i]->displayName), "same gear");
    CHECK(ra.range(1, 1000000) == rb.range(1, 1000000), "same random numbers drawn");
    CHECK(a.level == tuning().player.level && a.experience == tuning().player.experience && a.gold == tuning().player.gold, "a new run starts from the tuning");
    CHECK(a.charmees.empty() && a.score == 0 && a.steps == 0, "a new run clears charmees, score and steps");
    CHECK(a.scrolls.size() <= 1, "a new run clears the old scrolls");
  }
  std::printf(failures ? "loot_rules_test: %d failure(s)\n" : "loot_rules_test: ok\n", failures);
  return failures ? 1 : 0;
}
