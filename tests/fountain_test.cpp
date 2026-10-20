// The fountain: the per-run colour/effect shuffle, what a colour tells you after a drink, healing and stat changes.
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include "game/Fountain.hpp"

using namespace laby;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);           \
      return 1;                                                         \
    }                                                                   \
  } while (0)

int main() {
  Rng rng;
  rng.setSeed(11);
  const Fountain::Say quiet = [](const std::string&) {};

  // every effect belongs to exactly one colour, whatever the shuffle; a new run forgets the drinks
  Fountain f;
  for (int run = 0; run < 20; ++run) {
    f.newRun(rng);
    std::set<int> seen;
    for (int c = 0; c < Fountain::kColors; ++c) {
      f.roll(rng);
      seen.insert(static_cast<int>(f.effect()));
      CHECK(!f.known());
    }
    f.roll(rng);
    f.markDrunk();
    CHECK(f.known());
    CHECK(Fountain::colorName(f.color())[0] != 0);
  }
  {  // all seven effects appear across the seven colours
    f.newRun(rng);
    std::set<int> effects;
    for (int tries = 0; tries < 2000 && effects.size() < 7; ++tries) {
      f.roll(rng);
      effects.insert(static_cast<int>(f.effect()));
    }
    CHECK(effects.size() == 7);
  }

  // healing: tops up when hurt, makes you tougher when you were full already
  Hero h;
  h.roll(rng);
  h.hp = 1;
  h.mp = 0;
  Fountain::heal(h, quiet);
  CHECK(h.hp == h.maxHP && h.mp == h.maxMP);
  const int maxHP = h.maxHP, maxMP = h.maxMP;
  Fountain::heal(h, quiet);
  CHECK(h.maxHP > maxHP || h.maxMP > maxMP);
  CHECK(h.hp == h.maxHP && h.mp == h.maxMP);

  // a stat goes up by 1 + level / 4, and down no further than 1
  {
    Hero g;
    g.roll(rng);
    g.level = 8;
    double before = 0, after = 0;
    for (double b : g.base) before += b;
    Fountain::statChange(g, rng, +1, quiet);
    for (double b : g.base) after += b;
    CHECK(after == before + 3);
    for (double& b : g.base) b = 2;
    for (int i = 0; i < 10; ++i) Fountain::statChange(g, rng, -1, quiet);
    for (double b : g.base) CHECK(b >= 1);
  }
  {  // the message says what the stat actually did: a loss stops at 1, and a stat already there says so instead of "-3"
    int clamped = 0, unchanged = 0;
    for (int start : {1, 2, 3, 5}) {
      for (int sign : {+1, -1}) {
        for (int i = 0; i < 40; ++i) {
          Hero g;
          g.level = 8;  // 1 + 8/4 = 3 per drink
          for (double& b : g.base) b = start;
          const auto before = g.base;
          std::string said;
          Fountain::statChange(g, rng, sign, [&](const std::string& t) { said += t; });
          int moved = 0;
          for (size_t k = 0; k < g.base.size(); ++k) moved += static_cast<int>(std::abs(g.base[k] - before[k]));
          const size_t sgn = said.rfind(sign > 0 ? " +" : " -");
          if (moved == 0) {
            ++unchanged;
            CHECK(sgn == std::string::npos && said.find(sign > 0 ? "higher" : "lower") != std::string::npos);
          } else {
            CHECK(sgn != std::string::npos && std::atoi(said.c_str() + sgn + 2) == moved);
            clamped += moved < 3;
          }
        }
      }
    }
    CHECK(clamped > 0 && unchanged > 0);  // (both odd cases were exercised)
  }
  {  // the one list of stat names, in Stat order (screens, perk messages and fountain messages all read it)
    CHECK(std::string(statLongName(Stat::STR)) == "Strength" && std::string(statLongName(Stat::DEX)) == "Dexterity");
    CHECK(std::string(statLongName(Stat::INT)) == "Intelligence" && std::string(statLongName(Stat::VIT)) == "Vitality");
    CHECK(std::string(statLongName(Stat::CHA)) == "Charisma" && std::string(statLongName(std::size_t{2})) == "Intelligence");
  }
  std::printf("fountain_test: ok\n");
  return 0;
}
