#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "platform/Theme.hpp"
#include "render/CellGrid.hpp"
#include "ui/MainScreen.hpp"

namespace laby {

// The start screen and the character creation screen: a stormy night drawn from sub-pixel art (clouds drifting past, rain, lightning
// that flashes the whole sky), with the title or the creation form on top. Everything is a pure function of the time, so there is
// no state to keep; `ms` is the clock in milliseconds.

// The storm: the original's cloud sprites, rain and lightning over the whole grid, and (withArt) the original title picture: castle, moon, title. Returns true while a flash is lighting the sky.
bool drawStorm(CellGrid& grid, const Theme& theme, std::uint64_t ms, bool withArt = true);

// The start screen: the storm, the title, and a short list of choices with one highlighted. Row `big` ("Enter the Labyrinth") is the large one; rows above it (Continue) sit over it.
void drawTitleScreen(CellGrid& grid, const Theme& theme, std::uint64_t ms, const std::vector<std::string>& rows, int selected, int big = 0);

struct CreationInfo {
  std::string name;
  bool editing = false;                 // the name is being typed
  int selected = 0;                     // 0 name, 1 re-roll (stats and item together), 2 begin
  std::vector<OverlayLine> statLines;   // the five stats and what they give
  std::string itemTitle;
  std::vector<OverlayLine> itemLines;   // the starting item
};
void drawCreationScreen(CellGrid& grid, const Theme& theme, std::uint64_t ms, const CreationInfo& info);

// The top 10, arcade style: rank, name, floor and score. `highlight` (1-based, 0 for none) blinks.
struct LegendsInfo {
  struct Row { std::string name; int score = 0, floor = 0; };
  std::vector<Row> rows;  // best first, at most 10
  int highlight = 0;
  bool fromGameOver = false;
};
void drawLegendsScreen(CellGrid& grid, const Theme& theme, std::uint64_t ms, const LegendsInfo& info);

struct GameOverInfo {
  std::string name;
  std::vector<OverlayLine> facts;  // how it ended (killer, floor, level, steps)
  std::vector<OverlayLine> score;  // the score and its step bonus
};
// Game over: the storm again, with what killed you and the final score, and "Continue" under the cursor.
void drawGameOverScreen(CellGrid& grid, const Theme& theme, std::uint64_t ms, const GameOverInfo& info);

}  // namespace laby
