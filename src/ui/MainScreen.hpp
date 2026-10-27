#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>

#include "platform/Theme.hpp"
#include "render/CellGrid.hpp"
#include "render/Image8.hpp"
#include "ui/MapGlyphs.hpp"
#include "ui/TownArt.hpp"
#include "ui/ViewPainter.hpp"

namespace laby {

// The main screen (layout variant 4 "log strip", double-line outlines):
// the first-person view at the left, one right-hand column (minimap, or the action grid in a fight; the character; two charmee
// slots) and a full-width four-line log along the bottom. The panel sizes are in ScreenLayout.hpp.

// A charmed monster, for its slot in the right column.
struct CharmeeInfo {
  std::string name;
  int level = 1, hp = 1, maxHP = 1;
};

// The player's numbers for the HUD.
struct HudInfo {
  std::string name;
  std::string place;  // the title in the view's top border ("Dungeon 12", "Town", ...)
  int level = 1, floor = 1, hp = 1, maxHP = 1, mp = 0, maxMP = 0, ac = 0, gold = 0, xp = 0, xpNeeded = 1;
  int score = 0;
  bool hpHurt = false;  // you just took damage: the HP bar shows red for a moment (the terminal's: the lost part shows pale)
  int hpBefore = 0;     // the HP before that damage (since the last key press)
  std::array<int, 5> stats{};  // STR DEX INT VIT CHA
  std::vector<CharmeeInfo> charmees;  // at most two
};

// The minimap: kMinimapSize x kMinimapSize tiles around the player (the picture and its glyphs are in MapGlyphs.hpp).
constexpr int kMinimapSize = 7;
constexpr int kMinimapTile = 6;  // sub-pixels per tile: 21 x 11 cells, which fits where the map sits in the right column
using Minimap = MapPicture;

// A framed panel over the view (item details, the loot comparison): one or two columns of lines.
struct OverlayLine {
  std::string left, right;  // `right` is aligned to the column's right edge
  int tone = 0;             // 0 normal, 1 bright, 2 dim, 3 good, 4 bad, 5 gold
  int rightTone = -1;       // the right text's tone when it differs from `tone` (-1 = same)
};
struct OverlayColumn {
  std::string title;
  std::vector<OverlayLine> lines;
  std::string footer;
  std::vector<std::string> buttons;  // big panels: action-style buttons in place of the footer; `buttonSel` is highlighted
  int buttonSel = 0;
  std::string bigTitle;   // small boxes of the terminal screen: this in big letters (3 rows) at the top of the body, the lines under it
  bool selected = false;  // drawn with a bright frame
  int orb = 0;            // big panels: 1 = a health orb, 2 = a mana orb animated over the body (the lines go under it)
  double orbT = 0;        // seconds the orb has been showing
  int scroll = -1;        // big panels: >= 0 = a scroll picture (variant) over the body, the lines centred under it
  double scrollT = 0;     // seconds the scroll has been showing
};
struct OverlayBox {
  std::vector<OverlayColumn> columns;
  int smallW = 0, smallH = 0;  // > 0: a small box (in cells) in the top-right corner of the picture instead of covering it
};

// A sub-menu (Scrolls, Inventory): a list of buttons with a detail pane. Where it goes depends on `place`:
//  Left: a narrow panel at the left of the picture, list over details (the monster slides right so a fight stays visible).
//  Wide: the whole picture, list at the left and details at the right.
//  Page: the whole picture, a row of tabs over scrolling text (Help, About).
struct MenuRow {
  std::string left, right;  // `right` is aligned to the row's right edge
  int tone = 0;             // the right text's tone (see OverlayLine)
  bool enabled = true;      // false: dimmed (it can still be highlighted, to read why)
};
constexpr int kPageLines = 12;  // lines of text a Page shows at once (each two cell rows tall)
struct MenuPanel {
  // (Dialog and Right are only drawn by the terminal build, ui/TermScreen.cpp: a centred list, a list at the right edge of the picture.)
  enum class Place { Left, Wide, Page, Dialog, Right } place = Place::Left;
  std::string title;
  std::vector<MenuRow> rows;
  int selected = 0;
  int listW = 0;                    // Wide: the list's width in cells (0 = the default)
  std::vector<OverlayLine> extra;   // under the list (Wide: a block of facts about the character)
  std::string extraTitle;           // Wide: the label set into the rule over `extra`
  std::vector<OverlayLine> extraRight;  // Wide: a second column beside `extra`
  std::vector<OverlayLine> extraNote;  // Wide: one line under `extra`, set in pieces (left = text, tone = its tone)
  std::vector<OverlayLine> detail;  // about the selected row
  std::string footer;               // the key hint at the bottom
  int footerTone = 2;
  std::vector<std::string> tabs;       // Page: the tab titles
  int tab = 0;
  std::vector<OverlayLine> pageLines;  // Page: the text (tone 1 = heading, 2 = dim); `scroll` is the first line shown
  int scroll = 0;
};

// The fight in progress: the monster, and the action grid (Attack Charm Scroll Hide Trick Pray, in that order, two per row).
struct CombatHud {
  std::string name;
  int level = 1, hp = 1, maxHP = 1, ac = 0;
  std::array<int, 5> stats{};
  double hover = 0;                     // a flyer: floats this fraction of its height above the floor
  std::shared_ptr<const Image8> image;  // the monster art (2x), centered in the view
  int selected = 0;
  std::array<bool, 6> enabled{};
  bool over = false;  // the fight is over (grid hidden)
  std::string banner;  // a monster special action being announced (e.g. "LIFE DRAIN!"), empty when none
  double bannerT = 0;  // 0..1 through its time on screen
  int bannerKind = 0;  // MonsterAction as int: picks the color
  std::string bannerHint;  // a line under the banner (makes it taller): the death banner's "Press Enter to continue"
  bool boss10 = false;  // a Boss10: drawn bigger, red, in a pulsing glow
  double pulse = 0;     // 0..1, the glow's breathing
  std::string status; // the player's ailments ("POISONED 3  BLIND 2"), empty when fine
};

struct MainScreenOptions {
  const std::vector<OverlayLine>* guide = nullptr;  // town: the keys for where you are, in the map's place (there is no map in town); the first line is a heading
  const std::vector<TownSprite>* townSprites = nullptr;  // smooth pictures over the view (the sign post's signs)
  const std::vector<SceneLabel>* labels = nullptr;  // words on the picture (the town's signs and caption)
  const ViewLayers* view = nullptr;   // the first-person view (native size: kViewportCols*2 x kViewportRows*4 sub-pixels)
  const MapPicture* fullMap = nullptr;  // the whole floor, drawn over the view (the M key)
  int floorNumber = 0;                   // for the full map's title
  const Minimap* minimap = nullptr;   // none: an empty map panel (town)
  const std::vector<std::string>* log = nullptr;  // log lines, oldest first
  const HudInfo* hud = nullptr;
  const CombatHud* combat = nullptr;
  const OverlayBox* overlay = nullptr;
  const MenuPanel* menu = nullptr;
  bool bossGlow = false;     // a Boss10's lair is in view (or its entrance plays): the view's objects are drawn in the danger colour
  float bossGlowPulse = 0;   // 0..1, the glow's breathing
  bool torch = false;        // the torch's darkness at the picture's edges (a dungeon view)
  float torchShift = 0, torchPulse = 0, torchBreath = 0;  // the flicker's swing and darkness change (0 = calm)
  std::string hints;     // the key hints on the log's top rule, e.g. "[Arrows] Move  [Esc] Quit"
  std::string bottomHints;  // the system keys on the bottom row, left side (Scrolls, Inventory, Map, Sound)
  std::string statusTag;  // shown in the bottom border if not empty (current font / CRT level / cell size)
};

// Draws a sub-menu panel over a rectangle of the screen (cells), for screens that are not the main screen (the title, character creation).
void drawMenuOver(CellGrid& grid, const Theme& theme, const MenuPanel& menu, int x, int y, int cols, int rows);

// Draws the whole screen into `grid`, which must be kGridCols x kGridRows.
void drawMainScreen(CellGrid& grid, const Theme& theme, const MainScreenOptions& opt);

}  // namespace laby
