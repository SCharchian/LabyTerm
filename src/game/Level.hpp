#pragma once
#include <array>
#include <vector>

#include "core/Rng.hpp"
#include "game/Room.hpp"
#include "game/Tile.hpp"

namespace laby {

// State the Lua code kept in `Level.soilSpawnRate`, a *class-level* variable that outlives any single
// level: the last wall placed on one level changes the soil odds at the start of the next. It lives
// here (owned by the game session) so generation stays reproducible; keep one per run.
struct LevelGenState {
  double soilSpawnRate = 0.05;
};

struct LevelParams {
  int maxRooms = 10;
  int maxRoomSize = 5;       // largest room side in tiles (rooms are 3..this on a side)
  int scatteringFactor = 3;  // corridor wiggliness: each corridor tile has a scatteringFactor * 5% chance of a sidestep
  int maxSpan = 7;           // the widest open space allowed, in tiles: draw distance + 1 (caps maxRoomSize and is verified)
  int extraLoops = 2;        // corridors beyond the spanning tree
};

// One dungeon floor: a tile matrix plus the room tree, corridors, doors, elevator and secret door.
// Our own generator (the original's, a port of a third-party Lua one, was replaced for licensing):
//   1. rooms of several shapes (rectangle, chamfered, plus, L, pillared hall) are scattered with a wall gap between them;
//   2. a nearest-first spanning tree joins them, plus a few loop corridors; corridors are one tile wide and may wiggle;
//   3. the layout is checked (every room reachable, and no open mass wider than maxSpan in either direction) and rebuilt
//      from the same Rng stream until it passes;
//   4. walls, elevator, doors and the secret door are added.
// All randomness comes from the Rng passed to generate(), so a seed reproduces its dungeon.
class Level {
 public:
  static constexpr int kMinRoomSize = 3;
  static constexpr double kVeinSpawnRate = 0.02;

  Level(int height, int width);

  void setParams(const LevelParams& p);  // setMaxRooms/setMaxRoomSize/setScatteringFactor
  void generate(Rng& rng, LevelGenState& state);

  int height() const { return height_; }
  int width() const { return width_; }

  // Valid rows are -1..height+1, columns 0..width+1 (as in the Lua matrix); anything else throws,
  // where the Lua code would have crashed on a nil index.
  Tile& tile(int r, int c);
  const Tile& tile(int r, int c) const;
  bool isRoom(int r, int c) const { return tile(r, c).roomId != 0; }

  const std::vector<Room>& rooms() const { return rooms_; }
  const Room& room(int id) const { return rooms_.at(static_cast<size_t>(id - 1)); }
  int rootRoomId() const { return rootRoom_; }
  int endRoomId() const { return endRoom_; }
  // The room the level treasure goes in: a dead end deep in the tree, never the start or the boss room.
  int treasureRoomId() const { return treasureRoom_; }
  const std::vector<Pos>& entrances() const { return entrances_; }
  // Elevator position (x = column, y = row), as Level:getStaircases() returned it.
  int elevatorX() const { return staircases_.at(0).c; }
  int elevatorY() const { return staircases_.at(0).r; }
  bool hasSecretDoor() const { return secretSet_; }
  int secretX() const { return secretX_; }
  int secretY() const { return secretY_; }

  // Gameplay flags reset by generate(); the rest of the game flips them.
  bool secretOpen = false, secretRevealed = false;

  // The 9 positions around (r, c), including itself, in the Lua order (dx outer, dy inner).
  static std::array<Pos, 9> adjacent(int r, int c);

  // First FLOOR tile found scanning the 9 positions around `center` (used for boss/treasure/player).
  Pos floorNear(Pos center) const;

 private:
  bool tryLayout(Rng& rng, bool plain, bool strict);
  void resetMap();
  void placeRooms(Rng& rng, bool plain);
  void buildRoom(int r0, int c0, int h, int w, int shape, Rng& rng);
  void buildRoomTree();
  void pickSpecialRooms();
  bool carveCorridor(int fromId, int toId, Rng& rng, bool plain);
  void addLoops(Rng& rng, bool plain);
  bool allRoomsReachable() const;
  bool spansWithinLimit() const;
  void wallIn(Rng& rng);
  void placeWall(int r, int c, Rng& rng);
  void placeElevator(Rng& rng);
  void addDoors(Rng& rng);
  void placeSecretDoor(Rng& rng);
  bool isValidEntrance(int r, int c) const;
  bool makesFloorBlock(int r, int c) const;  // would turning (r, c) into floor complete a 2x2 block of floor?
  bool isPassable(int r, int c) const { return tile(r, c).cls == TileClass::FLOOR || tile(r, c).cls == TileClass::ELEVATOR; }
  static bool withinBounds(int r, int c, int height, int width) { return r < height && r > 0 && c < width && c > 0; }

  int height_, width_;
  int maxRoomSize_, maxRooms_, scatteringFactor_, maxSpan_ = 7, extraLoops_ = 2;
  std::vector<Tile> matrix_;  // (height + 3) rows (-1..height+1) x (width + 2) cols (0..width+1)
  std::vector<Room> rooms_;
  std::vector<Pos> entrances_, staircases_;
  int rootRoom_ = 0, endRoom_ = 0, treasureRoom_ = 0;
  int secretX_ = 0, secretY_ = 0;
  bool secretSet_ = false;
  LevelGenState* state_ = nullptr;  // valid only during generate()
};

}  // namespace laby
