#pragma once
#include <memory>
#include <string_view>
#include <vector>

#include "render/Cell.hpp"
#include "render/Image8.hpp"

namespace laby {

// One character of overlay text. Text is not tied to the cell grid: it sits at a pixel position made of the
// anchor cell (cx, cy) plus `col` / `row` steps of the *text pitch* (the current text size, which can be 1x,
// 1.5x, 1.83x or 2x the cell size; see Backend::setTextSixths). The glyph is blended over whatever is below.
struct TextRun {
  int cx = 0, cy = 0, col = 0, row = 0;
  char32_t ch = U' ';
  Rgb fg;
  float scale = 1.0f;  // glyph and pitch size on top of the text size (1.5 = one and a half times bigger)
  float dy = 0;        // extra distance down, in cell rows
  bool operator==(const TextRun&) const = default;
};

// A smooth picture drawn as real pixels over the cells (monsters and objects). Its top-left sits at the anchor
// cell plus (sx, sy) sub-pixels (half a cell wide, a quarter of a cell tall each, so the same coordinates as
// the mosaic art) and is scaled so that one *original* sprite pixel covers exactly one sub-pixel
// (`srcScale` image pixels per original pixel). Clipped to a cell rectangle.
struct ImageRun {
  std::shared_ptr<const Image8> img;
  int cx = 0, cy = 0, sx = 0, sy = 0, srcScale = 1;
  float scale = 1.0f;     // extra size factor on top of srcScale (0.75 = three quarters of the original size)
  int clipX0 = 0, clipY0 = 0, clipX1 = 0, clipY1 = 0;  // cells, end exclusive
  Rgb light, dark;
  bool bilinear = false;  // false: nearest-pixel sampling (crisp, aliased edges); true: soft edges
  bool glow = false;      // a monster: the CRT glow lights up whatever of it is brighter than `dark` (see GlowMask)
  bool operator==(const ImageRun&) const = default;
};

// The game's "framebuffer": a fixed-size grid of cells. Game code draws here and
// never talks to SDL; a Backend presents the grid.
class CellGrid {
 public:
  CellGrid(int w, int h) : w_(w), h_(h), cells_(static_cast<size_t>(w) * h) {}

  int width() const { return w_; }
  int height() const { return h_; }
  bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }

  const std::vector<Cell>& cells() const { return cells_; }
  Cell& at(int x, int y) { return cells_[static_cast<size_t>(y) * w_ + x]; }
  const Cell& at(int x, int y) const { return cells_[static_cast<size_t>(y) * w_ + x]; }

  // The colour the window's margins (the letterbox around the grid) are filled with: what the screen was last cleared to. (Not a cell's
  // own background: a cell under a drifting cloud would make the margin change colour.)
  Rgb background() const { return background_; }

  void clear(Rgb bg) {
    background_ = bg;
    for (auto& c : cells_) c = Cell{U' ', c.fg, bg};
    runs_.clear();
    images_.clear();
    viewport_ = Rect{};
    torch_ = Torch{};
  }
  void put(int x, int y, char32_t ch, Rgb fg, Rgb bg) {
    if (inBounds(x, y)) at(x, y) = Cell{ch, fg, bg};
  }
  void fill(int x, int y, int w, int h, char32_t ch, Rgb fg, Rgb bg) {
    for (int j = y; j < y + h; ++j)
      for (int i = x; i < x + w; ++i) put(i, j, ch, fg, bg);
  }
  // Single-line box using Unicode box-drawing glyphs. Interior is left untouched.
  void box(int x, int y, int w, int h, Rgb fg, Rgb bg);
  // UTF-8 text; returns the x just past the last glyph. No wrapping.
  int print(int x, int y, std::string_view utf8, Rgb fg, Rgb bg);
  // Overlay text (see TextRun): characters at text-pitch steps from the anchor cell, starting at column `col`
  // and row `row`. Returns the column just past the last character.
  int printText(int cx, int cy, int col, int row, std::string_view utf8, Rgb fg, float scale = 1.0f, float dy = 0);
  // Removes the overlay text whose first pixel lies in a cell rectangle (to put a panel over text that was already drawn).
  // `pitch` is the text size in cells per character (11/6 for the chosen 1.83x).
  void eraseText(int x, int y, int w, int h, double pitch) {
    std::erase_if(runs_, [&](const TextRun& r) {
      const double px = r.cx + r.col * pitch, py = r.cy + r.row * pitch;
      return px >= x && px < x + w && py >= y && py < y + h;
    });
  }
  void addImage(const ImageRun& run) { images_.push_back(run); }

  // The first-person viewport (cells, end exclusive). Screen effects treat it differently from the rest of the
  // UI: the CRT glow, for one, is strong on text and frames and gentle on the picture. Empty by default.
  struct Rect { int x0 = 0, y0 = 0, x1 = 0, y1 = 0; bool operator==(const Rect&) const = default; };
  void setViewport(int x0, int y0, int x1, int y1) { viewport_ = Rect{x0, y0, x1, y1}; }
  const Rect& viewport() const { return viewport_; }

  // The torch's light on the first-person picture (see the torch knobs): the picture darkens towards its edges, a
  // pixel pass in renderGrid. `rect` (cells) is the picture's area, `dark` the colour the edge fades to; `shift` and
  // `pulse` are the flicker's sideways swing and darkness change (0 = calm). Off by default.
  struct Torch {
    bool on = false;
    Rect rect;
    Rgb dark;
    float shift = 0, pulse = 0;
    std::vector<Rect> holes;  // cell rectangles the light does not darken: panels drawn over the picture
    float breath = 0;  // the calm torch breathing: the light's radius changes by this (fraction of the way to the edge)
    bool operator==(const Torch&) const = default;
  };
  void setTorch(const Torch& t) { torch_ = t; }
  const Torch& torch() const { return torch_; }
  Torch& torch() { return torch_; }
  // A panel over the picture: the torch leaves these cells alone so the panel stays readable.
  void addTorchHole(int x, int y, int w, int h) {
    if (torch_.on) torch_.holes.push_back(Rect{x, y, x + w, y + h});
  }

  const std::vector<TextRun>& textRuns() const { return runs_; }
  const std::vector<ImageRun>& imageRuns() const { return images_; }
  // True if both grids would render the same pixels (cells, overlay text and images).
  // ... apart from the torch's breathing and flicker
  bool sameAsExceptTorch(const CellGrid& o) const { return w_ == o.w_ && h_ == o.h_ && background_ == o.background_ && cells_ == o.cells_ && runs_ == o.runs_ && images_ == o.images_ && viewport_ == o.viewport_; }
  bool sameAs(const CellGrid& o) const { return w_ == o.w_ && h_ == o.h_ && background_ == o.background_ && cells_ == o.cells_ && runs_ == o.runs_ && images_ == o.images_ && viewport_ == o.viewport_ && torch_ == o.torch_; }

 private:
  Rgb background_{0, 0, 0};
  int w_, h_;
  std::vector<Cell> cells_;
  std::vector<TextRun> runs_;
  std::vector<ImageRun> images_;
  Rect viewport_;
  Torch torch_;
};

}  // namespace laby
