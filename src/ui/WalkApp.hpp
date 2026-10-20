#pragma once
#include <cstdint>

#include "ui/WalkSession.hpp"

namespace laby {

// `labyterm --walk [seed]`: walk a generated dungeon in first person, using the ported RenderWalls logic and the
// original sprites. Up/Down = step forward/back, Left/Right = turn, N = new dungeon, Esc = quit.
// (No monsters, items or combat yet: this is the dungeon and the view.)
int runWalkView(std::uint64_t seed);

}  // namespace laby
