#pragma once

namespace laby {

// The game's design grid, in cells (main-screen layout variant 4). A quarter-screen
// window (about 800x530 px) gets 6x12 cells (792x528 px) and a full-screen window gets exactly 12x24 (2x), with every size in
// between a whole number too.
constexpr int kGridCols = 132;
constexpr int kGridRows = 44;

// Overlay text size: 11/6 of the cell size, i.e. 11x22 px at 6x12 cells (1.83x). Chosen after comparing 1x to 2x.
constexpr int kTextSixths = 11;

// The panels, in cells. The whole screen has a one-cell outline; the first-person view sits inside it at the left, the right-hand
// column (minimap or actions, character, charmees) next to it, and the four-line log below the rule that runs under both.
constexpr int kRightCols = 36;                                 // the column's width including its left rule
constexpr int kViewportX = 1, kViewportY = 1;                  // top-left cell of the view
constexpr int kViewportCols = kGridCols - 3 - kRightCols;      // 93 cells = 186 sub-pixels (2x4 per cell)
constexpr int kViewportRows = 32;                              // 128 sub-pixels
constexpr int kLogRuleY = kViewportY + kViewportRows + 1;      // the rule between the view and the log (one black row below the view)
constexpr int kLogLines = 4;                                   // each is two cell rows of text

}  // namespace laby
