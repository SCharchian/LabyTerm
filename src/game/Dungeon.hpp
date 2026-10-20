#pragma once
#include <array>
#include <functional>
#include <vector>

#include "core/Rng.hpp"
#include "core/TuningParams.hpp"
#include "game/Level.hpp"

namespace laby {

struct XY {
  int x = 0, y = 0;  // x = column, y = row (the order the Lua Dungeon API used)
  constexpr bool operator==(const XY&) const = default;
};

// What a visited floor remembers beyond its seed: tiles seen, tiles that changed since generation (chests opened, the secret door),
// and the door flags. A floor is regenerated from its seed and this is applied on top.
struct FloorMemory {
  std::vector<bool> seen;                  // row-major, height x width
  std::vector<std::array<int, 3>> tiles;   // {x, y, TileClass} for each tile that differs from the freshly generated floor
  bool secretOpen = false, secretRevealed = false;
};

// The current dungeon floor plus what the game tracks about it (Lua: Dungeon.lua). Only one floor
// exists at a time; descending builds a new one. Positions here are (x, y); Level uses (row, col).
class Dungeon {
 public:
  using TreasureLooted = std::function<bool(int floor)>;

  Dungeon(int height, int width);

  int floor() const { return floor_; }
  Level& level() { return level_; }
  const Level& level() const { return level_; }

  // Level generation parameters for a given floor, from the tuning knobs (see dungeon.* in gameplay.json).
  static LevelParams firstFloorParams(const Tuning::Dungeon& t);
  static LevelParams paramsForFloor(const Tuning::Dungeon& t, int floor);

  // InitDungeon(): floor 1 with the first-floor parameters. Returns where the player starts.
  XY beginNewGame(Rng& rng, LevelGenState& gen, const Tuning::Dungeon& t, const TreasureLooted& looted);

  // ResetDungeon(): builds `floor` fresh (seen-tiles cleared), places boss and treasure, and returns
  // the elevator position (where the player arrives).
  XY beginFloor(int floor, Rng& rng, LevelGenState& gen, const Tuning::Dungeon& t, const TreasureLooted& looted);

  // Placement (called by the begin* functions; public for tests)
  XY initPlayer() const;
  void initBoss();
  void initLevelTreasure(const TreasureLooted& looted);

  // Tile access in (x, y). getTileType() also marks the tile seen, like the Lua version, and reports
  // soil and mineral veins as plain wall. Out of bounds reads as wall.
  TileClass getTileType(int x, int y);
  TileClass peekTileType(int x, int y) const;  // same, without marking it seen
  void setTileType(int x, int y, TileClass cls) { level_.tile(y, x).cls = cls; }

  // Which tiles the player has seen (drives the map).
  bool isTileSeen(int x, int y) const;
  void markTileSeen(int x, int y);
  bool inGrid(int x, int y) const { return x >= 1 && y >= 1 && x <= width_ && y <= height_; }
  // `under` is what the player at (px, py) stands on: the grid holds the player marker there, which is not part of the floor.
  FloorMemory remember(int px, int py, TileClass under) const;
  void recall(const FloorMemory& m);  // call on a freshly begun floor of the same number
  void wipeMiniMap();
  void revealStartingTiles();  // the 3x3 around the elevator

 private:
  void snapshotBase() { base_ = level_; }
  void rebuild(int floor, const LevelParams& p, Rng& rng, LevelGenState& gen);

  int height_, width_;
  int floor_ = 1;
  Level level_;
  Level base_;  // the floor as the begin* call left it, to see what changed since
  std::vector<bool> seen_;
};

}  // namespace laby
