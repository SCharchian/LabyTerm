#pragma once
#include "render/CellGrid.hpp"

namespace laby {

// An animated health / mana orb for the potion chest panel, drawn procedurally into the cells x..x+cols-1, y..y+rows-1
// (2x4 sub-pixels each, so square). `color` is the orb's hue (the HP or MP color), `t` seconds since the chest opened:
// a burst with a shock ring and flash in the first half second, then a breathing glass sphere with a bobbing liquid
// surface, bubbles, slow rays, and sparkles rising around it.
void drawOrb(CellGrid& g, int x, int y, int cols, int rows, Rgb color, Rgb paper, double t);

}  // namespace laby
