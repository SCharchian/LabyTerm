#pragma once
// The main screen laid out for a real terminal, where text is one
// cell wide and one row tall. The SDL game keeps the fixed layout of ScreenLayout.hpp; this one adapts to the
// terminal's size (at least 100 x 30 cells) and is only used when terminalUi() is set.
namespace laby {

constexpr int kTermMinCols = 100, kTermMinRows = 30;

// True in the terminal build (set once at startup by the app when its backend is a terminal).
inline bool& terminalUi() {
  static bool on = false;
  return on;
}

struct TermLayout {
  int W = 0, H = 0;                // the whole grid
  int vx = 1, vy = 1;              // top-left cell of the first-person view
  int vpCols = 0, vpRows = 0;      // its size in cells
  int ruleX = 0;                   // the vertical rule between the view and the right column
  int rx = 0, rw = 0;              // the right column's first cell and width (right of the rule)
  int ruleY = 0;                   // the rule between the view / column and the log
  int logLines = 4;                // rows of log, under the rule
};

// The right column is 28 cells wide (two 14-cell action buttons side by side); the log is four rows; the view
// gets everything else. Needs W >= kTermMinCols and H >= kTermMinRows.
inline TermLayout termLayout(int W, int H) {
  TermLayout l;
  l.W = W;
  l.H = H;
  l.rw = 28;
  l.ruleX = W - 2 - l.rw;
  l.rx = l.ruleX + 1;
  l.vpCols = l.ruleX - l.vx;
  l.logLines = 4;
  l.ruleY = H - 2 - l.logLines;
  l.vpRows = l.ruleY - l.vy;
  return l;
}

}  // namespace laby
