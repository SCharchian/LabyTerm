#pragma once
#include <cmath>
#include <vector>

#include "game/Tile.hpp"

namespace laby {

// A room is a node in the level's room tree (Lua: Room.lua). Neighbours are room ids (1-based).
struct Room {
  int id = 0;
  std::vector<int> neighbours;
  Pos center;
  bool hasStaircase = false;

  double distanceTo(const Room& other) const {
    const double dr = std::abs(center.r - other.center.r), dc = std::abs(center.c - other.center.c);
    return std::sqrt(dr * dr + dc * dc);
  }
};

}  // namespace laby
