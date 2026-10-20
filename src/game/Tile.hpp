#pragma once
#include "core/Enums.hpp"

namespace laby {

struct Pos {
  int r = 0, c = 0;  // row, column (the Lua code's [1], [2]); "y, x" in screen terms
  constexpr bool operator==(const Pos&) const = default;
};

// One map cell. `cls` is the symbol the original game stored (TileClass values are the map characters).
struct Tile {
  TileClass cls = TileClass::EMPTY;
  int roomId = 0;  // 0 = not part of a room

  bool isWall() const {
    return cls == TileClass::WALL || cls == TileClass::SECRET || cls == TileClass::SECRET_FOUND ||
           cls == TileClass::SOIL || cls == TileClass::VEIN;
  }
  bool isDoorway() const { return cls == TileClass::O_DOOR || cls == TileClass::C_DOOR; }
};

}  // namespace laby
