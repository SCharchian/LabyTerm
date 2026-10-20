// The run loop: step regeneration, and (below) game over.
#include <cstdio>

#include "platform/Theme.hpp"
#include "render/CellGrid.hpp"

#include "game/Hero.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);           \
      return 1;                                                         \
    }                                                                   \
  } while (0)

int main() {
  {  // 1 HP and 1 MP every 20 steps, never above the maximum, off when 0
    Hero h;
    h.maxHP = h.maxMP = 10;
    h.hp = h.mp = 5;
    for (h.steps = 1; h.steps < 20; ++h.steps) h.stepRegen(20);
    CHECK(h.hp == 5 && h.mp == 5);
    h.stepRegen(20);  // steps == 20
    CHECK(h.hp == 6 && h.mp == 6);
    h.hp = h.maxHP;
    h.steps = 40;
    h.stepRegen(20);
    CHECK(h.hp == 10 && h.mp == 7);
    h.steps = 60;
    h.stepRegen(0);
    CHECK(h.mp == 7);
  }
  {  // dying: the fight gives way to the game-over screen, the step bonus is added, Enter starts a new run
    WalkSession s(7);  // (a seed where the fight is not won before the hero can die: the monster rolls depend on the seed)
    s.debugHero().score = 100;
    s.debugHero().steps = 37;
    s.debugHero().hp = 0;
    s.debugFight();
    CHECK(s.inCombat() && !s.gameOver());
    s.handle(Key::Other, U'a');
    CHECK(s.dead() && s.gameOver() && !s.inCombat() && s.summary());
    const RunSummary r = *s.summary();
    CHECK(r.floor == 1 && r.steps == 37 && r.score == 100);
    CHECK(r.stepBonus == 37 * tuning().score.perStepTaken && r.finalScore == r.score + r.stepBonus);
    CHECK(!r.killedBy.empty() && r.killerLevel >= 1);
    CHECK(s.hero().score == r.finalScore);
    s.handle(Key::Up);  // the walking keys do nothing now
    CHECK(s.gameOver());
    s.handle(Key::Enter);
    CHECK(!s.gameOver() && !s.dead() && s.onTitle() && !s.summary());
    Theme theme;
    CellGrid grid(152, 40);
    s.draw(grid, theme, "");  // (the title) and, below, the game-over screen itself
    WalkSession d(6);
    d.debugHero().hp = 0;
    d.debugFight();
    d.handle(Key::Other, U'a');
    d.draw(grid, theme, "");
  }
  std::printf("run_test ok\n");
  return 0;
}
