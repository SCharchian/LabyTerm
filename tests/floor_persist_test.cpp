// Floors are the same dungeon every visit and across a save: layout, seen tiles, the level chest, the secret door, a killed boss.
// Each case records a floor the way it was left, goes elsewhere, comes back (and, after a save, comes back in a fresh session).
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/Tuning.hpp"
#include "game/Persist.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;
namespace fs = std::filesystem;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);           \
      return 1;                                                         \
    }                                                                   \
  } while (0)

// The floor as it stands: every tile (the player's marker read as what they stand on) and whether it was seen.
static std::string sig(const WalkSession& s) {
  std::string out;
  const Dungeon& d = s.dungeon();
  for (int y = 1; y <= tuning().dungeon.sizeY; ++y)
    for (int x = 1; x <= tuning().dungeon.sizeX; ++x) {
      TileClass c = d.level().tile(y, x).cls;
      if (c == TileClass::PLAYER) c = s.player().standingOn;
      out += std::to_string(static_cast<int>(c)) + (d.isTileSeen(x, y) ? "s," : ",");
    }
  return out;
}

// Player markers in the grid: exactly one while on the map (a stale one would be a ghost '@' left behind).
static int markers(const WalkSession& s) {
  int n = 0;
  for (int y = 1; y <= tuning().dungeon.sizeY; ++y)
    for (int x = 1; x <= tuning().dungeon.sizeX; ++x) n += s.dungeon().level().tile(y, x).cls == TileClass::PLAYER;
  return n;
}

// From town: The Labyrinth, to floor `floor` on the elevator, and off it. A floor whose boss is dead stays on the elevator.
static void ride(WalkSession& s, int floor) {
  for (int i = 0; i < 3; ++i) s.handle(Key::Down);
  s.handle(Key::Enter);
  for (int i = 0; i < 400 && s.elevatorLevel() != floor; ++i) s.handle(s.elevatorLevel() < floor ? Key::Down : Key::Up);
  s.handle(Key::Enter);
  s.debugSetPity(100000);  // no random fights while a test walks
}

// From a floor, standing on the elevator (the player's marker is still in the grid): straight to floor `floor`.
static void rideDirect(WalkSession& s, int floor) {
  s.debugRideElevator();
  for (int i = 0; i < 400 && s.elevatorLevel() != floor; ++i) s.handle(s.elevatorLevel() < floor ? Key::Down : Key::Up);
  s.handle(Key::Enter);
  s.debugSetPity(100000);
}

// Standing on the map, a few steps away from the elevator, and a look around.
static void wander(WalkSession& s, int salt) {
  for (int i = 0; i < 8; ++i) {
    s.handle(Key::Up);
    if (((i + salt) % 3) == 0) s.handle(Key::Right);
    if (((i + salt) % 5) == 0) s.handle(Key::Left);
  }
  s.debugExplore(20 + salt * 7);
}

static void save(WalkSession& s) {
  s.debugGoToTown();
  for (int i = 0; i < 4; ++i) s.handle(Key::Down);  // the sign post: Save and Exit is the fifth row
  s.handle(Key::Enter);
}

static std::unique_ptr<WalkSession> restore(const fs::path& dir) {
  auto s = std::make_unique<WalkSession>(777);  // (a different seed: the floors must come from the save)
  s->setStateDir(dir.string());
  s->startTitle();
  s->handle(Key::Enter);
  return s;
}

static fs::path freshDir(const char* name) {
  const fs::path dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  return dir;
}

// Several floors visited, none finished; saved; each is the same afterwards, in any order.
static int manyFloors() {
  const fs::path dir = freshDir("labyterm_floors_many");
  const std::vector<int> floors = {1, 4, 2, 7, 3, 12, 101};  // 101 shares floor 1's seed
  WalkSession a(5);
  a.setStateDir(dir.string());
  std::map<int, std::string> left;
  a.debugGoToTown();
  for (int f : floors) {
    ride(a, f);
    CHECK(a.floor() == f && !a.inTown());
    CHECK(markers(a) == 1 && a.debugCountTiles(TileClass::ELEVATOR) == 1);
    wander(a, f);
    CHECK(!a.inCombat());
    left[f] = sig(a);
    a.debugGoToTown();
  }
  for (size_t i = 0; i < floors.size(); ++i)
    for (size_t j = i + 1; j < floors.size(); ++j) CHECK(left[floors[i]] != left[floors[j]]);

  // back to each in the same session, in another order, before saving
  const std::vector<int> order = {12, 1, 7, 101, 2, 4, 3};
  for (int f : order) {
    ride(a, f);
    CHECK(a.floor() == f && markers(a) == 1 && a.debugCountTiles(TileClass::ELEVATOR) == 1);
    CHECK(sig(a) == left[f]);
    CHECK(!a.levels().bossDone(f));
    a.debugGoToTown();
  }

  // floor to floor on the elevator, never through town
  ride(a, 4);
  for (int f : {2, 4, 12, 3, 1}) {
    rideDirect(a, f);
    CHECK(a.floor() == f && markers(a) == 1 && a.debugCountTiles(TileClass::ELEVATOR) == 1);
    CHECK(sig(a) == left[f]);
  }
  save(a);
  CHECK(fs::exists(dir / "save.json"));
  auto b = restore(dir);
  CHECK(b->inTown());
  for (int f : {3, 101, 7, 1, 12, 4, 2}) {
    ride(*b, f);
    CHECK(b->floor() == f && !b->inTown());
    CHECK(markers(*b) == 1 && b->debugCountTiles(TileClass::ELEVATOR) == 1);
    CHECK(sig(*b) == left[f]);
    CHECK(!b->levels().bossDone(f));
    b->debugGoToTown();
  }
  // and the restored game can save again, and it still holds
  save(*b);
  auto c = restore(dir);
  for (int f : floors) {
    ride(*c, f);
    CHECK(sig(*c) == left[f]);
    c->debugGoToTown();
  }
  // a floor never visited is simply built
  ride(*c, 5);
  CHECK(c->floor() == 5 && c->debugCountTiles(TileClass::ELEVATOR) == 1);
  return 0;
}

// The level chest, once opened, stays opened: not rebuilt on a return, not after a save.
static int levelChest() {
  const fs::path dir = freshDir("labyterm_floors_chest");
  WalkSession a(5);
  a.setStateDir(dir.string());
  a.debugGoToTown();
  ride(a, 2);
  CHECK(a.debugCountTiles(TileClass::TREASURE) == 1);
  CHECK(a.debugGoNextTo(TileClass::TREASURE));
  a.handle(Key::Up);
  CHECK(a.atChest());
  for (int i = 0; i < 8 && a.atChest(); ++i) a.handle(Key::Enter);
  CHECK(!a.atChest() && a.levels().treasureDone(2));
  CHECK(a.debugCountTiles(TileClass::TREASURE) == 0);
  const std::string opened = sig(a);
  a.debugGoToTown();
  ride(a, 3);
  a.debugGoToTown();
  ride(a, 2);
  CHECK(a.debugCountTiles(TileClass::TREASURE) == 0 && sig(a) == opened);
  CHECK(markers(a) == 1);
  save(a);
  auto b = restore(dir);
  CHECK(b->levels().treasureDone(2));
  ride(*b, 2);
  CHECK(b->debugCountTiles(TileClass::TREASURE) == 0 && sig(*b) == opened);
  CHECK(!b->debugGoNextTo(TileClass::TREASURE));
  // an untouched floor keeps its chest through the same trip
  b->debugGoToTown();
  ride(*b, 3);
  CHECK(b->debugCountTiles(TileClass::TREASURE) == 1);
  const std::string three = sig(*b);
  b->debugGoToTown();
  save(*b);
  auto c = restore(dir);
  ride(*c, 3);
  CHECK(c->debugCountTiles(TileClass::TREASURE) == 1 && sig(*c) == three);
  return 0;
}

// The secret door, found and closed again: still a found secret (it pays once, the fountain keeps its colour) after a trip and a save.
static int secretDoor() {
  const fs::path dir = freshDir("labyterm_floors_secret");
  WalkSession a(5);
  a.setStateDir(dir.string());
  CHECK(a.debugGoNextTo(TileClass::SECRET));
  a.handle(Key::Up);
  CHECK(a.atFountain());
  const FountainEffect effect = a.fountainEffect();
  const int score = a.hero().score;
  CHECK(a.levels().secretPaid(1));
  a.handle(Key::Other, U'x');
  CHECK(!a.atFountain());
  const std::string closed = sig(a);
  a.debugGoToTown();
  ride(a, 2);
  a.debugGoToTown();
  ride(a, 1);
  CHECK(sig(a) == closed && a.debugCountTiles(TileClass::SECRET) == 1 && markers(a) == 1);
  save(a);
  auto b = restore(dir);
  ride(*b, 1);
  CHECK(sig(*b) == closed);
  CHECK(b->debugGoNextTo(TileClass::SECRET));
  b->handle(Key::Up);
  CHECK(b->atFountain() && b->fountainEffect() == effect);
  CHECK(b->hero().score == score);  // no second payment
  // passing the fountain shuts the door again, as it was
  b->handle(Key::Other, U'x');
  CHECK(sig(*b) == closed);
  return 0;
}

// A floor whose boss is dead cannot be entered again, before and after a save; the other floors are untouched.
static int bossKilled() {
  const fs::path dir = freshDir("labyterm_floors_boss");
  WalkSession a(3);
  a.setStateDir(dir.string());
  CHECK(a.debugGoNextTo(TileClass::BOSS));
  a.handle(Key::Up);
  CHECK(a.inCombat());
  for (int i = 0; i < 300 && a.inCombat() && !a.dead(); ++i) a.handle(Key::Other, U'w');  // (debug key: wins at once)
  CHECK(!a.dead());
  for (int i = 0; i < 4 && a.atChest(); ++i) a.handle(Key::Enter);
  CHECK(a.atPortal());
  a.handle(Key::Other, U'b');
  CHECK(a.levels().bossDone(1));
  a.debugGoToTown();
  std::map<int, std::string> left;
  for (int f : {2, 3}) {
    ride(a, f);
    wander(a, f);
    left[f] = sig(a);
    a.debugGoToTown();
  }
  ride(a, 1);  // refused: still in the elevator
  CHECK(a.inElevator());
  a.debugGoToTown();
  save(a);
  CHECK(a.hasSave());
  auto b = restore(dir);
  CHECK(b->inTown());
  CHECK(b->levels().bossDone(1) && !b->levels().bossDone(2));
  ride(*b, 1);
  CHECK(b->inElevator());  // still refused after the save
  b->debugGoToTown();
  for (int f : {3, 2}) {
    ride(*b, f);
    CHECK(sig(*b) == left[f] && markers(*b) == 1);
    b->debugGoToTown();
  }
  return 0;
}

int main() {
  if (int r = manyFloors()) return r;
  if (int r = levelChest()) return r;
  if (int r = secretDoor()) return r;
  if (int r = bossKilled()) return r;
  std::puts("floor_persist_test: all passed");
  return 0;
}
