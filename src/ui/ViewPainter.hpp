#pragma once
#include <algorithm>
#include <string>
#include <unordered_map>

#include "game/WallView.hpp"
#include "platform/Theme.hpp"
#include "render/CellGrid.hpp"
#include "render/Mosaic.hpp"

namespace laby {

// The first-person view as three 241x118 pixel layers, drawn the way the original drew its viewport image: a white
// canvas, the floor/ceiling background, the chosen sprites back to front, then the hard corner edges.
// SHADE: the standard darker tone of the drawing colour, for anything that is behind, inside or in shadow (the elevator's
// floor, its far walls and its rear ropes; later other objects' shaded parts). It is the accent mixed this far towards the
// background (0 = the background, 1 = the full accent); one number so everything shaded matches.
constexpr double kShadeMix = 0.34;
constexpr double kTextToneGamma = 0.7;  // the Text style draws every tone as amount^this: characters ink about half a cell, so the same tone looks dimmer; a curve (not a plain multiplier) keeps walls below the objects, which stay at the full accent

// The original's boss lair art (Monsters/1LevelBoss, 1LevelBossLeft, 1LevelBossRight): the cave mouth, and its two side views.
// Empty bitmaps draw a plain dark mound.
struct LairTextures {
  Bitmap1 front, left, right;
};

// Which theme color an object-layer fragment is drawn in (None: the accent, like everything else).
enum class Tint : std::uint8_t { None = 0, Treasure = 1, Lift = 2, Water = 3, Lair = 4 };
constexpr int kTints = 4;  // the tints other than None

// The theme colors of the tinted object groups (the elevator, chests, a fountain, the boss lair) and the accent that everything
// else uses. Null where a caller has no theme: every group then takes the accent.
struct ObjectColors {
  Rgb treasure, lift, water, lair, surround;
};

// The tinted groups' theme roles: gold for chests, the lift role for the elevator, mana for water, the lair role for the boss's lair.
inline ObjectColors objectColors(const Theme& th) { return {th.coin, th.lift, th.mana, th.lair, th.surround}; }

struct ViewLayers {
  Bitmap1 background;  // floor and ceiling (and the black band of "darkness" in the middle)
  Bitmap1 walls;       // walls, doors, secret doors
  Bitmap1 objects;     // chests, elevator carts and level bosses: drawn at full brightness over the walls
  Bitmap1 shade{};     // parts of objects in shade (procedural view only; empty otherwise): drawn in the shade tone (kShadeMix), above the background
  // native: the layers are already the size of the picture (the procedural view). Otherwise they are the original's
  // 241 x 118 and get cropped to the picture.
  bool native = false;
  // Object groups drawn in their own theme color (procedural view only; empty otherwise), indexed by Tint - 1: the full-brightness
  // layer and the shaded parts of the same objects.
  struct TintLayer { Bitmap1 lit, shade; };
  TintLayer tints[kTints]{};
  // Pixels per octant sub-pixel, across and down: 1 = the usual 2x4 per text cell (drawn as octant mosaics); 2 = 4x8 per cell,
  // drawn as ASCII text (the maze's "Text" style; only the procedural dungeon view is traced this finely).
  int density = 1;

  // Draws an original 241 x 118 sprite (a chest, the town portal, the elevator shaft) onto a layer: where the
  // original put it, or centred on the picture if the layers are native.
  // On a finer layer (density > 1) the sprite is enlarged by the density, so it keeps its size and place in the picture.
  void blitOriginal(Bitmap1& layer, const Bitmap1& sprite, int x, int y) const {
    const int d = std::max(1, density);
    const Bitmap1 big = d > 1 ? sprite.scaledTo(sprite.w * d, sprite.h * d) : Bitmap1();
    layer.blit(d > 1 ? big : sprite, x * d + (native ? (layer.w - 241 * d) / 2 : 0), y * d + (native ? (layer.h - 118 * d) / 2 : 0));
  }
};

// Draws a first-person view into the cell grid at (cx, cy): the one place that decides how the layers are coloured, so
// the game, the layout mock and the art workbench (tools/art.sh) all show the same thing. Top to bottom: objects at full
// brightness, walls and doors at mid brightness, the shade tone for shaded parts, floor and ceiling dimmest (all
// blends of the accent over the background).
// `objects`: a different colour for the object layer (a Boss10's lair is drawn in red).
// `colors`: the tinted groups' colors (null: all in the accent). A Boss10's glow also takes over the lair group.
void drawViewLayers(CellGrid& g, int cx, int cy, const ViewLayers& layers, Rgb accent, Rgb bg, const Rgb* objects = nullptr, const ObjectColors* colors = nullptr);

// Loads a bundled picture by its name without extension ("Town/Temple", "Laby-Title") from <data>/art/{title,town,lair,scenes}.
// Empty bitmap if nothing is found.
Bitmap1 loadOriginalArt(const std::string& name);

// Loads the original pictures on demand (see loadOriginalArt) and paints a view.
class ViewPainter {
 public:
  explicit ViewPainter(const WallView& view);

  // `combat` moves the top of the hard edge lines down to make room for the monster's name and health bar.
  ViewLayers paint(const WallView::Output& out, bool combat) const;

  // An original sprite by file name without extension (e.g. "Chest"), loaded on first use.
  const Bitmap1& image(const std::string& name) const;

  // The boss lair's pictures, for the procedural view (CorridorCamera::lair).
  const LairTextures& lair() const;

  static constexpr int kWidth = 241, kHeight = 118;

 private:
  const WallView& view_;
  mutable std::unordered_map<std::string, Bitmap1> cache_;
  mutable LairTextures lair_;
  mutable bool lairLoaded_ = false;
};

// True for the sprites that are objects (shown at full brightness): chests, elevator carts, level bosses.
bool isObjectSprite(const SpriteDef& s);

}  // namespace laby
