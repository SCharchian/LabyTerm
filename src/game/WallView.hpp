#pragma once
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Enums.hpp"

namespace laby {

// One sprite of the first-person view: where the original drew it (viewport pixels, top-left) and its z-order
// (depth 2 = 100, depth 1 = 200, depth 0 = 300, monsters 400). From data/view_sprites.json, which is extracted from
// the original SpriteManager.lua.
struct SpriteDef {
  std::string id;     // the original Lua variable name, e.g. "_1spriteFrontCenterWall"
  std::string image;  // original image path without "images/" and ".png"
  int x = 0, y = 0, z = 0;
};

// Which sprites make up the view, given the tiles in front of the player. A port of RenderWalls() from
// SpriteManager.lua, line for line (including its quirks); it was verified against the original Lua over many
// thousands of random tile configurations when it was written.
class WallView {
 public:
  // The three rows of tiles in front of the player, each left, centre, right of the player's facing direction:
  // row 0 is the row the player stands in (its centre is the player), row 1 is one step ahead, row 2 two steps.
  struct Input {
    std::array<TileClass, 3> row0{}, row1{}, row2{};
    bool secretFlicker = false;  // the torch is flickering (the original's currFlickerCount > 0): reveals a closed secret door
    bool secretOpen = false;     // the level's secret door has been opened (Level.fSecretOpen)
  };
  struct Output {
    std::vector<const SpriteDef*> sprites;  // back to front: by z, then in the order they were added
    bool hardLeft = false, hardRight = false;  // draw a hard vertical edge at the left / right corner (DrawHardWallEdge)
  };

  // Loads data/view_sprites.json. Throws std::runtime_error if it is missing or malformed.
  static WallView fromDataFile();
  explicit WallView(std::vector<SpriteDef> defs);

  Output render(const Input& in) const;

  const std::vector<SpriteDef>& sprites() const { return defs_; }
  const std::string& background() const { return background_; }
  // DrawHardWallEdge constants
  struct HardEdge { int leftX = 59, rightX = 181, yTop = 9, yTopCombat = 20, yBottom = 109; };
  const HardEdge& hardEdge() const { return hardEdge_; }

 private:
  const SpriteDef& def(const std::string& id) const;

  std::vector<SpriteDef> defs_;
  std::unordered_map<std::string, size_t> byId_;
  std::string background_;
  HardEdge hardEdge_;
};

}  // namespace laby
