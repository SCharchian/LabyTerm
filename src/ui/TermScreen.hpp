#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ui/MainScreen.hpp"
#include "ui/TitleScreen.hpp"

namespace laby {

// The main screen laid out for a terminal (TermLayout.hpp): text one cell
// wide and one row tall, sizes that follow the grid. `grid` must be at least kTermMinCols x kTermMinRows.
void drawMainScreenTerm(CellGrid& grid, const Theme& theme, const MainScreenOptions& opt);

// The same for the screens outside the dungeon (TitleScreen.hpp), and the system menu panel over a rectangle of the screen.
void drawTitleScreenTerm(CellGrid& grid, const Theme& theme, std::uint64_t ms, const std::vector<std::string>& rows, int selected, int bigRow,
                         const std::string& otherWay = "");  // otherWay: a line at the top right
void drawCreationScreenTerm(CellGrid& grid, const Theme& theme, std::uint64_t ms, const CreationInfo& info);
void drawLegendsScreenTerm(CellGrid& grid, const Theme& theme, std::uint64_t ms, const LegendsInfo& info);
void drawGameOverScreenTerm(CellGrid& grid, const Theme& theme, std::uint64_t ms, const GameOverInfo& info);
void drawMenuOverTerm(CellGrid& grid, const Theme& theme, const MenuPanel& menu, int x, int y, int cols, int rows);

}  // namespace laby
