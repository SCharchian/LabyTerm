// Scroll casting in a fight: MP, disintegration, and what each scroll does to the monster.
#include <cstdio>
#include <string>

#include "game/Combat.hpp"
#include "game/Scrolls.hpp"

using namespace laby;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);           \
      return 1;                                                         \
    }                                                                   \
  } while (0)

static Item scrollOf(ScrollType t) {
  for (const Item& s : ItemData::get().scrolls)
    if (s.scroll == t) return s;
  return {};
}

static Hero mage(Rng& rng) {
  Hero h;
  h.roll(rng);
  h.base[static_cast<size_t>(Stat::INT)] = 40;
  h.recalc();
  h.mp = h.maxMP = 200;
  for (const Item& s : ItemData::get().scrolls) h.addScroll(s);
  return h;
}

static Monster monsterOf(Rng& rng, MonsterClass cls) {
  for (int i = 0; i < 500; ++i) {
    Monster m = Monster::create(rng, 3, 3, false);
    if (m.cls == cls) {
      m.hp = m.maxHP = 100000;
      return m;
    }
  }
  return {};
}

int main() {
  Rng rng;
  rng.setSeed(5);
  std::string log;

  // not enough MP: nothing happens, nothing is spent
  {
    Hero h = mage(rng);
    h.mp = 0;
    Combat c(h, monsterOf(rng, MonsterClass::HUMAN), rng, [&](const std::string& s) { log += s + "\n"; });
    const int hp = c.monster().hp;
    c.cast(ScrollType::MagicArrow);
    CHECK(h.mp == 0 && c.monster().hp == hp && h.scrolls.size() == 8);
    CHECK(log.find("Insufficient MP!") != std::string::npos);
  }
  // damage scrolls hurt, spend MP, and sometimes crumble (the scroll is then gone)
  int crumbled = 0, casts = 0;
  for (int i = 0; i < 300; ++i) {
    Hero h = mage(rng);
    Combat c(h, monsterOf(rng, MonsterClass::HUMAN), rng, [](const std::string&) {});
    const int hp = c.monster().hp;
    c.cast(ScrollType::MagicMissile);
    CHECK(h.mp <= 200 - 4);
    if (!h.hasScroll(ScrollType::MagicMissile)) ++crumbled;
    else { CHECK(c.monster().hp < hp); ++casts; }
  }
  CHECK(crumbled > 0 && casts > 200);
  // Magic Arrow does double to the undead (same seed, same roll)
  {
    Rng a, b;
    a.setSeed(9);
    b.setSeed(9);
    Hero ha = mage(a), hb = mage(b);
    Monster ma = monsterOf(a, MonsterClass::UNDEAD), mb = ma;
    mb.cls = MonsterClass::HUMAN;
    int da = 0, db = 0;  // the damage dealt, from the log (an undead's reply may drain some of it back)
    const auto dmgOf = [](int& out) { return [&out](const std::string& m) { if (m.rfind("You hit the", 0) == 0) out = std::stoi(m.substr(m.find(" for ") + 5)); }; };
    Combat ca(ha, ma, a, dmgOf(da)), cb(hb, mb, b, dmgOf(db));
    a.setSeed(11);
    b.setSeed(11);
    ca.cast(ScrollType::MagicArrow);
    cb.cast(ScrollType::MagicArrow);
    CHECK(ha.hasScroll(ScrollType::MagicArrow) && hb.hasScroll(ScrollType::MagicArrow));
    CHECK(da > 0 && da == 2 * db);
  }
  // Holy Water: useless on the living, deadly to the undead
  int ashes = 0;
  for (int i = 0; i < 100; ++i) {
    Hero h = mage(rng);
    Combat living(h, monsterOf(rng, MonsterClass::HUMAN), rng, [](const std::string&) {});
    living.cast(ScrollType::HolyWater);
    CHECK(living.monster().hp == 100000 || h.mp < 197);  // (hp untouched)
    CHECK(living.outcome() == CombatOutcome::Ongoing);
    Hero h2 = mage(rng);
    Combat dead(h2, monsterOf(rng, MonsterClass::UNDEAD), rng, [](const std::string&) {});
    dead.cast(ScrollType::HolyWater);
    ashes += dead.outcome() == CombatOutcome::Won;
  }
  CHECK(ashes > 40);
  // Sleep and Feeble Mind change the monster; ESP raises the flag
  int slept = 0;
  for (int i = 0; i < 100; ++i) {
    Hero h = mage(rng);
    Combat c(h, monsterOf(rng, MonsterClass::ANIMAL), rng, [](const std::string&) {});
    c.cast(ScrollType::Sleep);
    slept += c.monster().has(StatusFlag::SLEEPING);
  }
  CHECK(slept > 20);
  {
    Hero h = mage(rng);
    Monster m = monsterOf(rng, MonsterClass::HUMAN);
    m.stats[static_cast<size_t>(Stat::INT)] = 30;
    Combat c(h, m, rng, [](const std::string&) {});
    c.cast(ScrollType::FeebleMind);
    if (h.hasScroll(ScrollType::FeebleMind)) CHECK(c.monster().stat(Stat::INT) < 30);
    Combat e(h, m, rng, [](const std::string&) {});
    e.cast(ScrollType::ESP);
    if (h.hasScroll(ScrollType::ESP)) CHECK(e.takeEsp() && !e.takeEsp());
  }
  // Wizard Eye and Invisibility are not fight scrolls: nothing happens
  {
    Hero h = mage(rng);
    Combat c(h, monsterOf(rng, MonsterClass::HUMAN), rng, [](const std::string&) {});
    c.cast(ScrollType::WizardEye);
    CHECK(h.mp == 200);
  }
  std::printf("scroll_test OK\n");
  return 0;
}
