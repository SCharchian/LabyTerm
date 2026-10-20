// The secret door and its fountain: it opens by itself, offers a drink, each colour's effect, and closing it again.
#include <cstdio>
#include <vector>

#include "core/Tuning.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);           \
      return 1;                                                         \
    }                                                                   \
  } while (0)

// A session standing at the floor's secret door, which has just been found (no clock: the door is open at once).
static bool atSecret(WalkSession& s) {
  if (!s.debugGoNextTo(TileClass::SECRET)) return false;
  s.handle(Key::Up);
  return true;
}

int main() {
  {  // stepping into the wall finds the door; it is open, the fountain is on offer, and X closes it again (it is a wall once more)
    WalkSession s(5);
    CHECK(atSecret(s));
    CHECK(s.atFountain() && !s.secretMoving());
    const int score = s.hero().score;
    CHECK(score > 0);
    s.handle(Key::Other, U'x');
    CHECK(!s.atFountain());
    CHECK(s.debugCountTiles(TileClass::SECRET) == 1 && s.debugCountTiles(TileClass::SECRET_FOUND) == 0);
    s.handle(Key::Up);  // found again (still facing it): offered again, but the find pays out only once
    CHECK(s.atFountain() && s.hero().score == score);
  }
  {  // with a clock the door moves for secretDoorMs and ignores keys; then the fountain is offered
    WalkSession s(5);
    s.tick(0);
    CHECK(s.debugGoNextTo(TileClass::SECRET));
    s.handle(Key::Up);
    CHECK(s.secretMoving() && !s.atFountain());
    s.handle(Key::Enter);  // ignored while it slides
    CHECK(s.secretMoving());
    CHECK(s.tick(1000));
    CHECK(s.tick(2000) && s.secretMoving());
    CHECK(s.tick(5000) && s.atFountain());  // (the slide began at the first tick after the step: 1000)
    CHECK(!s.tick(6000));                   // nothing moves while the offer waits
    s.handle(Key::Other, U'x');             // closing: half the time
    CHECK(s.secretMoving());
    s.tick(6000);
    s.tick(8000);
    CHECK(!s.secretMoving() && s.debugCountTiles(TileClass::SECRET) == 1);
  }
  {  // the fountain's teleport never lands on the elevator (or anything but plain floor), in any room of any floor: the elevator can sit
     // on the start room's centre, and nothing fires without a step, so the hero would stand on it with no way to ride
    int centresNotFloor = 0, rooms = 0;
    for (std::uint64_t seed = 1; seed <= 150; ++seed) {
      WalkSession s(seed);
      const int n = static_cast<int>(s.dungeon().level().rooms().size());
      for (int id = 1; id <= n; ++id) {
        ++rooms;
        centresNotFloor += s.dungeon().level().tile(s.dungeon().level().room(id).center.r, s.dungeon().level().room(id).center.c).cls != TileClass::FLOOR;
        s.debugTeleportToRoom(id);
        CHECK(s.player().standingOn == TileClass::FLOOR);
      }
    }
    CHECK(centresNotFloor > 0);  // (the case this guards against does happen: some room centres are not floor)
    std::printf("secret_test: teleport checked in %d rooms, %d with a non-floor centre\n", rooms, centresNotFloor);
  }
  {  // the find pays once per floor for the whole run: leaving the floor and coming back does not pay again
    WalkSession s(5);
    CHECK(atSecret(s));
    const int paid = s.hero().score;
    CHECK(paid > 0 && s.levels().secretPaid(1));
    s.handle(Key::Other, U'x');
    for (int round = 0; round < 2; ++round) {  // away to floor 2, then back to a rebuilt floor 1
      if (s.player().standingOn != TileClass::ELEVATOR) {  // (a new floor starts you on its elevator)
        CHECK(s.debugGoNextTo(TileClass::ELEVATOR));
        s.handle(Key::Up);
      }
      if (!s.inElevator()) s.debugRideElevator();
      CHECK(s.inElevator());
      s.handle(round == 0 ? Key::Down : Key::Up);
      s.handle(Key::Enter);
      CHECK(!s.inElevator() && s.floor() == (round == 0 ? 2 : 1));
    }
    const int before = s.hero().score;
    CHECK(atSecret(s));
    CHECK(s.hero().score == before);  // no second payment on the returned-to floor 1 ...
    CHECK(s.levels().secretPaid(1) && !s.levels().secretPaid(2));
    const std::vector<int> floors = s.levels().marked(LevelTracker::Kind::Secret);  // ... and it is what a save keeps
    CHECK(floors.size() == 1 && floors[0] == 1);
  }
  {  // every fight, the fountain's shadow included, is followed by a few steps free of random encounters
    bool shadow = false;
    for (std::uint64_t seed = 1; seed < 400 && !shadow; ++seed) {
      WalkSession s(seed);
      if (!atSecret(s) || !s.atFountain() || s.fountainEffect() != FountainEffect::ShadowFight) continue;
      shadow = true;
      s.debugSetPity(0);
      s.handle(Key::Enter);
      CHECK(s.inCombat());
      CHECK(s.pityLeft() == tuning().encounters.pitySteps && s.pityLeft() > 0);
    }
    CHECK(shadow);
  }
  // every effect, over seeds until each has been seen
  bool seen[8] = {};
  for (std::uint64_t seed = 1; seed < 400; ++seed) {
    WalkSession s(seed);
    if (!atSecret(s) || !s.atFountain()) continue;
    const FountainEffect e = s.fountainEffect();
    if (seen[static_cast<int>(e)]) continue;
    seen[static_cast<int>(e)] = true;
    Hero& h = s.debugHero();
    h.hp = 1;
    h.mp = 0;
    const int score = h.score;
    const double sum = h.base[0] + h.base[1] + h.base[2] + h.base[3] + h.base[4];
    const int px = s.player().x, py = s.player().y;
    s.handle(Key::Enter);
    CHECK(!s.atFountain());
    CHECK(s.hero().score > score);
    switch (e) {
      case FountainEffect::Heal: CHECK(s.hero().hp == s.hero().maxHP && s.hero().mp == s.hero().maxMP); break;
      case FountainEffect::StatUp:
      case FountainEffect::StatDown: {
        const Hero& g = s.hero();
        const double now = g.base[0] + g.base[1] + g.base[2] + g.base[3] + g.base[4];
        CHECK(e == FountainEffect::StatUp ? now > sum : now < sum);
        break;
      }
      case FountainEffect::Teleport: CHECK(s.player().x != px || s.player().y != py); break;
      case FountainEffect::ShadowFight: CHECK(s.inCombat()); break;
      case FountainEffect::Invisibility: CHECK(s.invisible()); break;
      case FountainEffect::Alarm: CHECK(s.alarmOn()); break;
    }
    if (e != FountainEffect::Teleport && e != FountainEffect::ShadowFight) {
      // the fountain is drained: the door stays open and nothing is offered again
      s.handle(Key::Up);
      CHECK(!s.atFountain());
    }
  }
  for (int e = 1; e <= 7; ++e) CHECK(seen[e]);
  std::printf("secret_test: ok\n");
  return 0;
}
