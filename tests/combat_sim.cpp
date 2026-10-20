// Simulates many fights (random hero vs. random monsters of one dungeon floor) to check that combat runs to a
// conclusion without surprises, and to give a feel for the balance.
//   combat_sim [floor=1] [fights=2000] [attack|trick] [log]   (log: also print every message of the first fights)
#include <cstdio>
#include <cstdlib>
#include <string>

#include "game/Combat.hpp"

using namespace laby;

int main(int argc, char** argv) {
  const int floor = argc > 1 ? std::atoi(argv[1]) : 1, fights = argc > 2 ? std::atoi(argv[2]) : 2000;
  const bool trick = argc > 3 && std::string(argv[3]) == "trick";
  const bool verbose = argc > 4 && std::string(argv[4]) == "log";
  Rng rng(12345);
  int won = 0, died = 0, fled = 0, stuck = 0;
  long turns = 0, hpLeft = 0;
  for (int i = 0; i < fights; ++i) {
    Hero h;
    h.roll(rng);
    h.level = floor;  // a hero of about the floor's level (stats are the starting 8..18 plus level-ups below)
    for (int l = 1; l < floor; ++l) h.base[static_cast<size_t>(rng.range(1, 5)) - 1] += 2;
    h.recalc();
    h.hp = h.maxHP;
    Combat c(h, Monster::create(rng, floor, floor, i % 10 == 9), rng, [&](const std::string& m) { if (verbose && i < 3) std::printf("  %s\n", m.c_str()); });
    if (verbose && i < 3) std::printf("fight %d: hero HP %d STR %.0f DEX %.0f vs the monster\n", i, h.hp, h.stat(Stat::STR), h.stat(Stat::DEX));
    int t = 0;
    while (c.outcome() == CombatOutcome::Ongoing && t < 500) {
      c.act(trick ? CombatAction::Trick : CombatAction::Attack);
      ++t;
    }
    turns += t;
    switch (c.outcome()) {
      case CombatOutcome::Won: ++won; hpLeft += h.hp; break;
      case CombatOutcome::Died: ++died; break;
      case CombatOutcome::Fled: ++fled; break;
      default: ++stuck; break;
    }
  }
  std::printf("floor %d, %d fights: won %d, died %d, monster fled %d, unresolved %d | avg %.1f turns, avg HP left after a win %.1f\n", floor, fights, won, died, fled, stuck,
              double(turns) / fights, won ? double(hpLeft) / won : 0.0);
  return stuck == 0 ? 0 : 1;  // every fight must reach an outcome
}
