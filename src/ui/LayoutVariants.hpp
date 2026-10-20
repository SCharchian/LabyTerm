#pragma once
#include "platform/Theme.hpp"
#include "render/CellGrid.hpp"

namespace laby {

// Main-screen layout proposals, drawn with made-up sample data so they can be compared side by
// side. Each variant has its own grid size (cells are 6x12 px at the default size), so the window shape differs:
//   1  "Side panels":  the current idea made taller (about 1.4:1): character + charmees left, minimap + scroll list right.
//   2  "Square":       full-width view, then a scroll hotbar, then three columns below (about 1:1).
//   3  "Immersive":    the view fills the window with HUD chips drawn over it, the controls in a strip below (about 4:3).
//   4  "Log strip":   a full-width 4-line log along the bottom; one right column (minimap, or the actions in a fight).
//   5  "Context panel": (about 1:1.1) a status row under the view whose middle switches between minimap and actions.
//   6  "Cinematic":    the view fills the window, chips hover over it, the log has its own strip, actions pop up in a fight.
// Variants 4-6 have no scroll list on the main screen: the Scrolls screen (`scrollsScreen`) opens over the view when casting,
// and equipment lives on an Inventory screen (not mocked).
constexpr int kLayoutVariantCount = 6;

void layoutVariantGrid(int variant, int& cols, int& rows);
int layoutVariantActionCols(int variant);  // the action grid is 2 wide in variant 1, 3 wide in the others

// `combat` shows the fight state (enemy bar, action grid active); `sel` is the highlighted action (0..5) and `scroll`
// the highlighted scroll (0..7).
void drawLayoutVariant(CellGrid& grid, const Theme& theme, int variant, bool combat, int sel = 0, int scroll = 2, bool scrollsScreen = false,
                       int lineStyle = 0);  // lineStyle: 0 thin, 1 heavy, 2 double

// `labyterm --layouts [variant 1-6] [combat]`: one variant in a live window (C = combat, arrows move the action
// highlight, [ ] move the scroll highlight, S = Scrolls screen, L = line style, Esc = quit).
int runLayoutVariants(int variant, bool combat);

}  // namespace laby
