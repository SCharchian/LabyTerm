#pragma once
#include <memory>
#include <string>
#include <vector>

#include "render/Image8.hpp"
#include "ui/ViewPainter.hpp"

namespace laby {

// The town's pictures: the original's bitmaps (Town/SignPost, Temple, Blacksmith, BeastMart, the signs and the candle flames), fitted
// to the picture. The square is the town's menu (its sign post), the shops draw their own box inside the picture, as in the original,
// so none of it is a system panel. Text on the picture is real text (`SceneLabel`), placed by the main screen over the art.
enum class TownScene { Square, Temple, Blacksmith, BeastMart };

// One line of text on the picture, two cell rows tall: `text` placed by `align` in the cells (cx, cy, w), and `right` (if any) at the
// right end. `filled` is the highlighted row (solid, text in the background colour). Tones are the overlay tones (see OverlayLine).
struct SceneLabel {
  enum class Align { Left, Center, Right } align = Align::Center;
  bool filled = false;
  bool button = false;  // drawn as one of the game's buttons (4 rows tall, `filled` = the chosen one) instead of a text line
  int cx = 0, cy = 0, w = 0;
  int h = 2;           // cell rows: 2 in the SDL window; the terminal's text lines are 1 (its text sits on the last row)
  int tone = 0, rightTone = 0;
  float fade = 0.0f;   // 0-1: how far the text colour is faded towards what it is drawn on (a faint mark beside the real text)
  float scale = 1.0f;  // letter size (1.5 = big); big text is drawn letter by letter and only the left text is supported
  std::string text, right;
};

// A picture drawn as smooth pixels over the mosaic instead of as part of it (the sign post's signs: the original's 1-bit bitmaps,
// enlarged `kSignDetail` times and softened). (x, y): its top-left corner in the picture's sub-pixels. `lit`: at full brightness (the chosen sign).
constexpr int kSignDetail = 4;
struct TownSprite {
  std::shared_ptr<const Image8> img;
  int x = 0, y = 0;
  bool lit = false;
};

struct TownPicture {
  ViewLayers layers;
  std::vector<TownSprite> sprites;  // over the layers
  std::vector<SceneLabel> labels;  // filled in by the caller (it knows the hero, the stock...)
};

// The shops' box (BeastMart, Blacksmith), in cells of the picture: the art draws its outline; the text goes inside it.
// The box starts at the same cell in every view size (the art to its left is a fixed size) and runs to the right edge: 51 cells in the SDL
// window's 93, 28 in the terminal's smallest view.
constexpr int kShopBoxX = 42;
struct ShopGeom {
  int innerX, innerW;  // the cells inside the box, for text
  int slotRows;        // each of BeastMart's two slots, in cell rows (15 of the SDL window's 32)
};
inline ShopGeom shopGeom(int cols, int rows) { return {kShopBoxX + 2, cols - kShopBoxX - 4, (rows - 2) / 2}; }

// `selected`: the sign that is highlighted (the square). `flame`: the candle flames' frame (0-6, the Temple). `density`: 1 = the pixel
// art (octant mosaic), 2 = the Text style (the layers hold 4x8 pixels per cell, drawn as characters; the scenery is then dimmer, and the
// signs are not drawn as smooth pictures).
// A view wider than the pictures are is not stretched: the scene is centred, `townMarginCols` cells in from each side. (The sign post
// shows more of the town to its left as the view grows, up to 120 cells.)
int townDesignCols(TownScene scene);
int townMarginCols(TownScene scene, int cols);
TownPicture paintTown(const ViewPainter& painter, TownScene scene, int cols, int rows, int selected = 0, int flame = 0, int density = 1);

}  // namespace laby
