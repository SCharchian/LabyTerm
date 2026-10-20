#pragma once
#include <array>

#include "game/Dungeon.hpp"
#include "game/WallView.hpp"

namespace laby {

// What happened when the player tried to step.
enum class StepResult {
  Moved,           // took the step
  Bumped,          // a wall: no move ("Bonk")
  FoundSecret,     // stepped at a secret wall: it is now an open secret door; no move
  SecretOpenAlready  // the secret door was already found; no move
};

// The player's place in the dungeon and how they move: a port of Player:StepForward / StepBackward / TurnLeft /
// TurnRight (Player.lua). The level matrix holds a '@' on the player's tile; the tile the player stands on is
// remembered and put back when they step off. Gameplay events (elevator, treasure, boss, doors) are reported by
// `standingOn` and `doorOpened`; acting on them is the caller's job.
struct Walker {
  // The map steps for a facing: one step ahead is (ax, ay), one step to the player's right is (sx, sy).
  struct Basis { int ax, ay, sx, sy; };
  static Basis basisFor(Direction f) {
    static constexpr Basis kBasis[4] = {{0, -1, 1, 0}, {1, 0, 0, 1}, {0, 1, -1, 0}, {-1, 0, 0, -1}};
    return kBasis[static_cast<int>(f)];
  }

  int x = 0, y = 0;
  Direction facing = Direction::NORTH;
  TileClass standingOn = TileClass::FLOOR;
  bool doorOpened = false;  // the last step opened a door (C_DOOR became O_DOOR)

  // Places the player on (x, y) facing `facing` and puts the '@' marker there.
  void place(Dungeon& d, int px, int py, Direction f);
  // Takes the player off the map: puts back the tile they were standing on (before placing them elsewhere on the
  // same floor).
  void lift(Dungeon& d);
  StepResult stepForward(Dungeon& d);
  StepResult stepBackward(Dungeon& d);
  void turnLeft() { facing = static_cast<Direction>((static_cast<int>(facing) + 3) % 4); }
  void turnRight() { facing = static_cast<Direction>((static_cast<int>(facing) + 1) % 4); }

 private:
  StepResult step(Dungeon& d, int dx, int dy, bool forward);
};

// The three rows of tiles in front of the player, as DrawDungeon() in main.lua collects them (reading the tiles marks
// them as seen on the map, as in the original).
WallView::Input viewTiles(Dungeon& d, const Walker& w);

}  // namespace laby
