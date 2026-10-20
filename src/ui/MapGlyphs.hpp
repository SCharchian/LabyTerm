#pragma once
#include <vector>

#include "platform/Theme.hpp"
#include "render/CellGrid.hpp"

namespace laby {

// The minimap's picture: each tile is a small flat-colour pictogram (a vector shape sampled at the tile's sub-pixels), drawn as
// octant mosaic cells. Sub-pixels are square (2x4 per cell, 3x3 px at 6x12), so a tile of `size` sub-pixels is square on screen.
// A cell can show only two colours: tiles that are a whole number of cells tall (size 4, 8, 12) keep a tile's colours apart; other
// sizes share cells between tiles, and each cell then takes its two most common colours. Sizes 4 and 6 use hand-drawn pixel
// pictograms; 8 and up are drawn as vector shapes.
enum class MapTile : unsigned char {
  Unseen, Wall, Floor, DoorClosed, DoorOpen, Fountain, FountainDry, Elevator, Boss, Chest,
  Player,  // drawn on a floor tile, pointing `facing`
};

struct MapPicture {
  int cols = 0, rows = 0;               // tiles
  std::vector<MapTile> tile;            // row by row
  int facing = 0;                       // 0 north, 1 east, 2 south, 3 west
  MapTile at(int c, int r) const { return tile[static_cast<size_t>(r * cols + c)]; }
};

// Cells covered by a picture of `tiles` tiles of `size` sub-pixels (rounded up).
inline int mapCellsWide(int tiles, int size) { return (tiles * size + 1) / 2; }
inline int mapCellsTall(int tiles, int size) { return (tiles * size + 3) / 4; }

// Draws the picture with its top-left cell at (cx, cy).
void drawMapPicture(CellGrid& g, const Theme& th, Rgb bg, int cx, int cy, const MapPicture& map, int size);

}  // namespace laby
