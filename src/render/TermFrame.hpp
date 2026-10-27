#pragma once
#include "render/CellGrid.hpp"

namespace laby {

// What a terminal can show of `g`: a copy where the pictures (ImageRun) are folded into the cells as 2x4 octant
// mosaics (two colors per cell, like the mosaic art) and the overlay text sits on the cell grid, one character per
// cell. The result has no overlays left. Mosaic cells use the game's private-use glyphs (termGlyph maps them).
CellGrid terminalFrame(const CellGrid& g);

}  // namespace laby
