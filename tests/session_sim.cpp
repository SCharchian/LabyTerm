// A random bot plays many headless sessions (random keys, including fights, chests and the item views) and draws every
// frame, to shake out crashes and stuck states.
//   session_sim [seeds=20] [keys=4000]
#include <cstdio>
#include <cstdlib>

#include "ui/WalkSession.hpp"

using namespace laby;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);           \
      return 1;                                                         \
    }                                                                   \
  } while (0)

// A scripted run through the floors: kill the boss, ride the elevator (a cleared floor refuses you), reach town by
// riding up, come back down, return to a floor that is already built.
// There is exactly one elevator on a floor: at the start of a game and on every floor you ride to.
static int oneElevator() {
  for (int seed = 1; seed <= 40; ++seed) {
    WalkSession s(static_cast<std::uint64_t>(seed));
    CHECK(s.debugCountTiles(TileClass::ELEVATOR) == 1);
    CHECK(s.player().standingOn == TileClass::ELEVATOR);  // you start on it
    for (int f = 2; f <= 4; ++f) {
      s.debugRideElevator();
      for (int i = 1; i < f; ++i) s.handle(Key::Down);  // ride from floor 1 (or the floor you are on) one deeper each time
      s.handle(Key::Enter);
      CHECK(s.floor() >= 1);
      CHECK(s.debugCountTiles(TileClass::ELEVATOR) == 1);
    }
  }
  return 0;
}

static int floorsScript() {
  WalkSession s(3);
  CHECK(s.floor() == 1);
  CHECK(s.debugGoNextTo(TileClass::BOSS));
  s.handle(Key::Up);
  CHECK(s.inCombat());
  for (int i = 0; i < 300 && s.inCombat() && !s.dead(); ++i) s.handle(Key::Other, U'a');
  CHECK(!s.dead());
  for (int i = 0; i < 4 && s.atChest(); ++i) s.handle(Key::Enter);
  CHECK(s.atPortal());
  s.handle(Key::Other, U'b');
  CHECK(!s.atPortal() && s.levels().bossDone(1));
  CHECK(s.debugGoNextTo(TileClass::ELEVATOR));
  s.handle(Key::Up);
  CHECK(s.inElevator());
  s.handle(Key::Enter);  // floor 1 is cleared: refused
  CHECK(s.inElevator() && s.floor() == 1);
  s.handle(Key::Down);
  s.handle(Key::Down);
  s.handle(Key::Enter);
  CHECK(!s.inElevator() && s.floor() == 3);
  s.debugRideElevator();
  CHECK(s.inElevator());
  for (int i = 0; i < 2 && s.inElevator(); ++i) s.handle(Key::Up);  // down to floor 1
  CHECK(s.inElevator() && !s.inTown());
  s.handle(Key::Up);  // one more: the readout says TOWN, but you are still in the shaft
  CHECK(s.inElevator() && !s.inTown());
  s.handle(Key::Enter);  // and Enter gets off in town
  CHECK(s.inTown() && !s.inElevator());
  for (int i = 0; i < 3; ++i) s.handle(Key::Down);  // the sign post: The Labyrinth
  s.handle(Key::Enter);  // the town's elevator
  CHECK(s.inElevator());
  s.handle(Key::Down);  // floor 1 (cleared)
  s.handle(Key::Enter);
  CHECK(s.inElevator());
  s.handle(Key::Down);  // floor 2
  s.handle(Key::Enter);
  CHECK(!s.inElevator() && s.floor() == 2);
  s.debugRideElevator();
  s.handle(Key::Enter);  // the same floor again: no new dungeon, back at the elevator
  CHECK(!s.inElevator() && s.floor() == 2 && s.player().standingOn == TileClass::ELEVATOR);
  return 0;
}

int main(int argc, char** argv) {
  if (const int rc = oneElevator()) return rc;
  if (const int rc = floorsScript()) return rc;
  const int seeds = argc > 1 ? std::atoi(argv[1]) : 20, keys = argc > 2 ? std::atoi(argv[2]) : 4000;
  Theme theme;
  CellGrid grid(152, 40);
  long fights = 0, chests = 0, deaths = 0, items = 0;
  for (int s = 1; s <= seeds; ++s) {
    WalkSession session(static_cast<std::uint64_t>(s));
    Rng bot(1000 + s);
    static const Key kKeys[] = {Key::Up, Key::Up, Key::Up, Key::Down, Key::Left, Key::Right, Key::Enter, Key::Enter, Key::Other};
    static const char kLetters[] = "acshtprin";
    bool wasFight = false, wasChest = false;
    for (int k = 0; k < keys; ++k) {
      if (session.dead()) {
        ++deaths;
        session.newDungeon(static_cast<std::uint64_t>(s) * 977 + static_cast<std::uint64_t>(k));
      }
      if (k % 500 == 250) session.debugGoNextTo(k % 1000 == 250 ? TileClass::TREASURE : TileClass::BOSS);
      const Key key = kKeys[bot.range(0, 8)];
      const char32_t ch = key == Key::Other ? static_cast<char32_t>(kLetters[bot.range(0, 8)]) : 0;
      if (ch == U'n') continue;  // a new dungeon on a letter would hide state transitions we want to see
      session.handle(key, ch);
      if (k % 7 == 0) session.draw(grid, theme, "sim");
      if (session.inCombat() && !wasFight) ++fights;
      if (session.atChest() && !wasChest) ++chests;
      wasFight = session.inCombat();
      wasChest = session.atChest();
    }
    for (const auto& e : session.hero().equipment) items += e ? 1 : 0;
  }
  std::printf("%d sessions x %d keys: %ld fights, %ld chests, %ld deaths, %ld items worn at the end\n", seeds, keys, fights, chests, deaths, items);
  return 0;
}
