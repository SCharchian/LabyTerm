#include "ui/LayoutVariants.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "game/WallView.hpp"
#include "ui/CorridorRenderer.hpp"
#include "ui/PanelKit.hpp"
#include "ui/ViewPainter.hpp"

namespace laby {
namespace {

// ---- sample data (all made up; the real game feeds the same shapes) -----------------------------------------------

struct SampleScroll {
  const char* name;       // as listed
  const char* shortName;  // for a slot (6 characters)
  int mp;
  const char* desc;
  bool carried;           // the player holds at most one of each kind
  int levelReq, intReq;
  bool utility;           // usable outside combat (the others are combat-only)
};
// The eight scroll kinds, in ScrollType order (names, costs and requirements are the real ones).
const std::array<SampleScroll, 8> kScrolls = {{
    {"Magic Arrow", "MagArw", 2, "Damages monster based on your INT. Inflicts bonus damage on the Undead.", true, 1, 12, false},
    {"Holy Water", "HolyWt", 3, "Kills any Undead monster outright (if it succeeds)", false, 1, 13, false},
    {"Sleep", "Sleep", 5, "Casts a veil of slumber on foes, making them easy targets.", true, 1, 13, false},
    {"Mag Missile", "MagMsl", 4, "Conjure ethereal bolts of arcane energy with deadly accuracy.", true, 2, 15, false},
    {"Feeble Mind", "Feeble", 3, "Cloud your enemy's mind with arcane fog, lowering their INT.", false, 3, 17, false},
    {"Wizard Eye", "WizEye", 5, "Unveil the hidden halls and chambers of the map.", false, 3, 17, true},
    {"ESP", "ESP", 1, "A psychic scan of the monster's abilities. (this is a free turn!)", true, 1, 13, false},
    {"Invisibility", "Invis", 5, "Your form becomes a mere whisper in the air. (Mostly) undetectable.", true, 1, 13, true},
}};

const char* kAction[6] = {"Attack", "Charm", "Scroll", "Hide", "Trick", "Pray"};

const std::array<std::string, 11> kMap = {
    "###########",
    "#....#....#",
    "#.##.#.##.#",
    "#.#......E#",
    "#.#.####.##",
    "#...#^.#..#",   // '^' = the player, facing north
    "###.#..#.##",
    "#...####..#",
    "#.#.......#",
    "#.#.#####.#",
    "###########",
};

struct SampleLog {
  std::vector<std::string> normal = {"You descend to level 12.", "A heavy door stands ahead.", "The air smells of old stone.",
                                     "You hear dripping water.", "Something stirs far away."};
  std::vector<std::string> normalWide = {"You descend to level 12. The air grows colder with every step.",
                                          "A heavy iron-banded door stands ahead of you, slightly ajar.",
                                          "Somewhere far below, something large shifts in its sleep.",
                                          "You hear dripping water echoing through the corridor ahead.",
                                          "Your torch gutters, then steadies."};
  std::vector<std::string> fightWide = {"A Dragon blocks your path and lowers its great horned head!",
                                        "You swing your Flame Blade at the Dragon: 8 damage.",
                                        "The Dragon bites you for 5 damage. You stagger back.",
                                        "Your Rat bites the Dragon: 3 damage.", "You swing at the Dragon: MISS."};
  std::vector<std::string> fight = {"A Dragon blocks your path!", "You swing: 8 damage.", "The Dragon bites: 5 damage!", "Rat bites: 3 damage.",
                                    "You swing: MISS."};
};

// ---- drawing helpers ---------------------------------------------------------------------------------------------

// The first-person view at any size, from the real procedural renderer: a corridor with a door ahead, or (combat) an open
// room where the eye has eased back. No monster art here: the mock is about where things go.
void drawView(Ctx& c, int x, int y, int cols, int rows, bool combat) {
  const std::array<std::array<TileClass, 3>, 3> tiles = {{
      {TileClass::WALL, TileClass::PLAYER, TileClass::WALL},
      {TileClass::WALL, TileClass::FLOOR, TileClass::WALL},
      {TileClass::WALL, combat ? TileClass::FLOOR : TileClass::C_DOOR, TileClass::WALL},
  }};
  const TileLookup lookup = [&](int a, int r) {
    TileInfo t;
    t.mx = r;
    t.my = -a;
    t.tile = (a >= 0 && a <= 2 && r >= -1 && r <= 1) ? tiles[static_cast<size_t>(a)][static_cast<size_t>(r + 1)] : TileClass::WALL;
    return t;
  };
  CorridorCamera cam;
  cam.fovDegrees = 95;
  cam.drawDistance = 6;
  cam.supersample = 2;
  if (combat) cam.backOff = 0.45;
  const ObjectColors oc = objectColors(c.th);
  drawViewLayers(c.g, x, y, toLayers(renderCorridor(cols * 2, rows * 4, lookup, cam)), c.hi, c.bg, nullptr, &oc);
  c.g.setViewport(x, y, x + cols, y + rows);  // screen effects treat the picture differently from the UI
}

// The enemy's name and health across the top of the view (the final art is the rounded gauge in LayoutMock.cpp).
void drawEnemyBar(Ctx& c, int x, int y, int w) {
  c.clear(x, y, w, 2);
  c.text(x + 1, y, "Dragon  L12", c.th.danger);
  c.textR(x + 1, y, w - 2, "64/120 HP", c.fg);
  c.bar(x, y + 2, w, 0.53, c.th.danger, mix(c.bg, c.th.danger, 0.18));
}

// The 11x11-tile minimap, each tile two cells wide so the picture is square, with an arrow for the player.
void drawMinimap(Ctx& c, int x, int y) {
  const Rgb wall = mix(c.bg, c.dim, 0.75), floor = mix(c.bg, c.dim, 0.45);
  for (int r = 0; r < 11; ++r)
    for (int q = 0; q < 11; ++q) {
      const char t = kMap[static_cast<size_t>(r)][static_cast<size_t>(q)];
      const int cx = x + q * 2;
      switch (t) {
        case '#': c.g.put(cx, y + r, U'▓', wall, c.bg); c.g.put(cx + 1, y + r, U'▓', wall, c.bg); break;
        case '^':  // the player, pointing the way they face: ◢◣ is an up arrow two cells wide
          c.g.put(cx, y + r, U'◢', c.hi, c.bg); c.g.put(cx + 1, y + r, U'◣', c.hi, c.bg); break;
        case 'E':  // the elevator: a wireframe cabin
          c.g.put(cx, y + r, U'[', c.th.mana, c.bg); c.g.put(cx + 1, y + r, U']', c.th.mana, c.bg); break;
        default: c.g.put(cx, y + r, U'·', floor, c.bg); c.g.put(cx + 1, y + r, U' ', floor, c.bg);
      }
    }
}

// The character. A narrow panel (< 30 cells) puts the level on its own line; `compact` stops after the bars.
void drawHero(Ctx& c, int x, int y, int w, bool compact) {
  const int bw = std::max(4, w - 16);
  const bool wide = w >= 30;
  c.text(x, y, "D. Crawley", c.hi);
  if (wide) c.textR(x, y, w, "Lv 7", c.dim);
  else y += 2, c.text(x, y, "Lv 7  Floor 12", c.dim);
  c.text(x, y + 2, "HP 42/50", c.fg);
  c.bar(x + 16, y + 2, bw, 0.84, c.th.good, mix(c.bg, c.th.good, 0.2));
  c.text(x, y + 4, "MP 11/20", c.fg);
  c.bar(x + 16, y + 4, bw, 0.55, c.th.mana, mix(c.bg, c.th.mana, 0.2));
  if (compact) return;
  c.text(x, y + 6, "STR 12  DEX  9", c.fg);
  c.text(x, y + 8, "INT 14  VIT 11", c.fg);
  c.text(x, y + 10, "CHA  8  AC   6", c.fg);
  c.text(x, y + 12, "Gold 1234", c.th.coin);
  c.text(x, y + 14, "XP 380/412", c.dim);
  c.bar(x, y + 16, w, 380.0 / 412, c.dim, mix(c.bg, c.dim, 0.25));
}

// A charmed monster: name and level, its HP bar, and a few key numbers. `w` cells wide, three rows tall.
void drawCharmee(Ctx& c, int x, int y, int w, const char* name, int lvl, int hp, int maxHp, bool withStats) {
  const std::string title = std::string(name) + " L" + std::to_string(lvl), hpText = std::to_string(hp) + "/" + std::to_string(maxHp);
  c.text(x, y, title, c.fg);
  if (static_cast<int>((title.size() + hpText.size() + 1) * 11 / 6) <= w) c.textR(x, y, w, hpText, hp * 3 <= maxHp ? c.th.danger : c.dim);
  c.bar(x, y + 2, w, static_cast<double>(hp) / maxHp, c.th.good, mix(c.bg, c.th.good, 0.2));
  if (withStats) c.text(x, y + 3, "ATK 4  AC 2", c.dim);
}

// One scroll slot: key in the border, short name inside; empty slots stay visible so positions never move.
void drawScrollSlot(Ctx& c, int x, int y, int w, int idx, bool selected) {
  const SampleScroll& s = kScrolls[static_cast<size_t>(idx)];
  const Rgb col = !s.carried ? mix(c.bg, c.dim, 0.5) : selected ? c.hi : c.fg;
  c.clear(x, y, w, 3);
  c.rect(x, y, w, 3, selected ? c.hi : (s.carried ? c.line : mix(c.bg, c.line, 0.4)));
  c.g.print(x + 1, y, std::to_string(idx + 1), selected ? c.hi : c.dim, c.bg);
  if (s.carried && selected) c.g.fill(x + 1, y + 1, w - 2, 1, U' ', c.fg, c.th.selection);
  const std::string label = s.carried ? s.shortName : "-";
  c.g.printText(x + 1, y + 1, std::max(0, (w - 2 - static_cast<int>(label.size()) * 11 / 6) / 2), 0, label, col);
}

// The six actions as a grid (3x2 or 2x3) of buttons. Arrow keys move the highlight; Enter/Space/A choose.
void drawActionGrid(Ctx& c, int x, int y, int bw, int sel, bool active, int cols) {
  for (int i = 0; i < 6; ++i) {
    const int bx = x + (i % cols) * bw, by = y + (i / cols) * 3;
    const bool on = i == sel && active, usable = active || i == 2;
    c.clear(bx, by, bw, 3);
    c.rect(bx, by, bw, 3, on ? c.hi : usable ? c.line : mix(c.bg, c.line, 0.4));
    if (on) c.g.fill(bx + 1, by + 1, bw - 2, 1, U' ', c.fg, c.th.selection);
    const std::string label = kAction[i];
    c.g.printText(bx + 1, by + 1, std::max(0, (bw - 2 - static_cast<int>(label.size()) * 11 / 6) / 2), 0, label,
                  on ? c.hi : usable ? c.fg : mix(c.bg, c.dim, 0.5));
  }
}

void drawLog(Ctx& c, int x, int y, int w, int lines, const std::vector<std::string>& log) {
  const int total = static_cast<int>(log.size()), first = std::max(0, total - lines);
  for (int i = first; i < total; ++i) {
    const int r = (i - first) * 2;
    c.text(x, y + r, "> ", c.dim);
    c.text(x + 4, y + r, log[static_cast<size_t>(i)].substr(0, static_cast<size_t>(std::max(0, c.chars(w - 4)))), i == total - 1 ? c.hi : c.fg);
  }
}

// ---- variant 1: side panels --------------------------------------------------------------------------------------

void variant1(Ctx& c, bool combat, int sel, int scroll) {
  const int W = c.g.width(), H = c.g.height();
  const int lw = 26, vw = 84, vh = 30;  // left panel, view width/height
  const int vx = 1 + lw + 1, vy = 1, rx = vx + vw + 1, rw = W - 1 - rx;
  c.frame(0, 0, W, H, "LABYRINTH", c.line);
  c.g.print(W - 22, 0, "[ Dungeon 12 ]", c.hi, c.bg);
  const int ruleY = vy + vh;
  for (int y = 1; y < H - 1; ++y) {
    c.g.put(vx - 1, y, U'│', c.line, c.bg);
    c.g.put(rx - 1, y, U'│', c.line, c.bg);
  }
  for (int x = vx; x < rx - 1; ++x) c.g.put(x, ruleY, U'─', c.line, c.bg);
  c.g.put(vx - 1, ruleY, U'├', c.line, c.bg);
  c.g.put(rx - 1, ruleY, U'┤', c.line, c.bg);
  c.g.put(vx - 1, 0, U'┬', c.line, c.bg);
  c.g.put(rx - 1, 0, U'┬', c.line, c.bg);
  c.g.put(vx - 1, H - 1, U'┴', c.line, c.bg);
  c.g.put(rx - 1, H - 1, U'┴', c.line, c.bg);

  drawView(c, vx, vy, vw, vh, combat);
  if (combat) drawEnemyBar(c, vx, vy, vw);

  // left: character, then the two charmees
  drawHero(c, 2, 2, lw - 2, false);
  c.g.print(2, 24, "CHARMEES", c.hi, c.bg);
  drawCharmee(c, 2, 26, lw - 2, "Rat", 5, 7, 9, true);
  drawCharmee(c, 2, 31, lw - 2, "Goblin", 8, 14, 20, true);
  c.text(2, H - 4, "[I] Inventory", c.dim);
  c.text(2, H - 6, "[Esc] Menu", c.dim);

  // right: minimap, the scrolls carried, and what the highlighted one does
  drawMinimap(c, rx + (rw - 22) / 2, 2);
  c.g.print(rx + 1, 14, "SCROLLS", c.hi, c.bg);
  int row = 0, selRow = -1;
  for (int i = 0; i < 8; ++i) {
    if (!kScrolls[static_cast<size_t>(i)].carried) continue;
    const bool on = i == scroll;
    if (on) { selRow = row; c.g.fill(rx, 15 + row * 2, rw, 2, U' ', c.fg, c.th.selection); }
    c.text(rx + 1, 15 + row * 2, std::string(on ? ">" : " ") + kScrolls[static_cast<size_t>(i)].name, on ? c.hi : c.fg);
    ++row;
  }
  (void)selRow;
  const SampleScroll& s = kScrolls[static_cast<size_t>(scroll)];
  const int by = 15 + 8 * 2 + 1;
  c.frame(rx, by, rw, H - 1 - by, s.name, c.line);
  c.text(rx + 1, by + 1, "MP " + std::to_string(s.mp), c.th.mana);
  const auto lines = wrap(s.desc, c.chars(rw - 2));
  for (size_t i = 0; i < lines.size() && by + 3 + static_cast<int>(i) * 2 < H - 2; ++i) c.text(rx + 1, by + 3 + static_cast<int>(i) * 2, lines[i], c.fg);

  // bottom centre: log and the action grid
  c.g.print(vx + 1, ruleY, "[ LOG ]", c.hi, c.bg);
  SampleLog sl;
  const int gx = vx + vw - 2 * 13 - 2;
  drawLog(c, vx + 1, ruleY + 2, gx - vx - 3, 9, combat ? sl.fight : sl.normal);
  c.g.print(gx - 1, ruleY, combat ? "[ ACTION ]" : "[ ACTIONS ]", c.hi, c.bg);
  drawActionGrid(c, gx, ruleY + 2, 13, sel, combat, 2);
  if (!combat) c.text(gx, ruleY + 12, "Fight actions: enemy needed", c.dim);
}

// ---- variant 2: square -------------------------------------------------------------------------------------------

void variant2(Ctx& c, bool combat, int sel, int scroll) {
  const int W = c.g.width(), H = c.g.height();
  const int vw = W - 2, vh = 30, vy = 1;
  c.frame(0, 0, W, H, "LABYRINTH", c.line);
  c.g.print(W - 22, 0, "[ Dungeon 12 ]", c.hi, c.bg);
  drawView(c, 1, vy, vw, vh, combat);
  if (combat) drawEnemyBar(c, 1, vy, vw);

  // the scroll hotbar: eight fixed slots (empty ones dim), the highlighted one described underneath
  const int hy = vy + vh;  // rule row
  for (int x = 1; x < W - 1; ++x) c.g.put(x, hy, U'─', c.line, c.bg);
  c.g.put(0, hy, U'├', c.line, c.bg);
  c.g.put(W - 1, hy, U'┤', c.line, c.bg);
  c.g.print(2, hy, "[ SCROLLS ]", c.hi, c.bg);
  const int sw = 14, sx0 = (W - 8 * sw) / 2;
  for (int i = 0; i < 8; ++i) drawScrollSlot(c, sx0 + i * sw, hy + 1, sw, i, i == scroll);
  const SampleScroll& s = kScrolls[static_cast<size_t>(scroll)];
  const auto desc = wrap(std::string(s.name) + " (MP " + std::to_string(s.mp) + "): " + (s.carried ? s.desc : "not carried"), c.chars(8 * sw));
  for (size_t i = 0; i < desc.size() && i < 2; ++i) c.text(sx0, hy + 4 + static_cast<int>(i) * 2, desc[i], s.carried ? c.fg : c.dim);
  const int ry = hy + 8;  // second rule
  for (int x = 1; x < W - 1; ++x) c.g.put(x, ry, U'─', c.line, c.bg);
  c.g.put(0, ry, U'├', c.line, c.bg);
  c.g.put(W - 1, ry, U'┤', c.line, c.bg);

  // three columns: character + charmees | log + actions | minimap
  const int lw = 38, cx = 1 + lw + 1, cw = 42, mx = cx + cw + 1;
  for (int y = ry + 1; y < H - 1; ++y) {
    c.g.put(cx - 1, y, U'│', c.line, c.bg);
    c.g.put(mx - 1, y, U'│', c.line, c.bg);
  }
  for (int x : {cx - 1, mx - 1}) {
    c.g.put(x, ry, U'┼', c.line, c.bg);
    c.g.put(x, H - 1, U'┴', c.line, c.bg);
  }
  drawHero(c, 2, ry + 1, lw - 2, true);
  c.text(2, ry + 7, "STR12 DEX 9 INT14", c.fg);
  c.text(2, ry + 9, "VIT11 CHA 8  AC 6", c.fg);
  c.text(2, ry + 11, "Gold 1234  XP 380/412", c.dim);
  drawCharmee(c, 2, ry + 14, 17, "Rat", 5, 7, 9, false);
  drawCharmee(c, 21, ry + 14, 17, "Goblin", 8, 14, 20, false);
  SampleLog sl;
  drawLog(c, cx + 1, ry + 1, cw, 4, combat ? sl.fight : sl.normal);
  drawActionGrid(c, cx + 1, ry + 10, 13, sel, combat, 3);
  drawMinimap(c, mx + (W - 1 - mx - 22) / 2, ry + 1);
  c.text(mx + 1, ry + 14, "Score 9400", c.dim);
  c.text(mx + 1, ry + 16, "[Esc] Menu", c.dim);
}

// ---- variant 3: immersive ----------------------------------------------------------------------------------------

void variant3(Ctx& c, bool combat, int sel, int scroll) {
  const int W = c.g.width(), H = c.g.height();
  const int vw = W - 2, vh = 32, vy = 1;
  c.frame(0, 0, W, H, "LABYRINTH", c.line);
  c.g.print(W - 22, 0, "[ Dungeon 12 ]", c.hi, c.bg);
  drawView(c, 1, vy, vw, vh, combat);
  if (combat) drawEnemyBar(c, 1, vy, vw);
  const int top = combat ? 4 : 1;

  // HUD chips over the picture: character and charmees top left, minimap top right, the log bottom left
  c.clear(2, top + 1, 31, 7);
  c.frame(2, top + 1, 31, 7, "", c.line);
  c.text(4, top + 2, "D. Crawley", c.hi);
  c.textR(4, top + 2, 27, "Lv 7", c.dim);
  c.text(4, top + 4, "HP 42/50", c.fg);
  c.bar(19, top + 4, 13 - 1, 0.84, c.th.good, mix(c.bg, c.th.good, 0.2));
  c.text(4, top + 6, "MP 11/20", c.fg);
  c.bar(19, top + 6, 13 - 1, 0.55, c.th.mana, mix(c.bg, c.th.mana, 0.2));
  c.clear(2, top + 8, 31, 8);
  c.frame(2, top + 8, 31, 8, "", c.line);
  drawCharmee(c, 4, top + 9, 27, "Rat", 5, 7, 9, false);
  drawCharmee(c, 4, top + 13, 27, "Goblin", 8, 14, 20, false);
  c.clear(W - 26, top + 1, 25, 13);
  c.frame(W - 26, top + 1, 25, 13, "", c.line);
  drawMinimap(c, W - 25, top + 2);
  SampleLog sl;
  const int ly = vy + vh - 8;
  c.clear(2, ly, 70, 7);
  c.frame(2, ly, 70, 7, "", c.line);
  drawLog(c, 4, ly + 1, 66, 2, combat ? sl.fight : sl.normal);

  // the control strip: scrolls (2 x 4 slots) | actions | numbers
  const int sy = vy + vh;
  for (int x = 1; x < W - 1; ++x) c.g.put(x, sy, U'─', c.line, c.bg);
  c.g.put(0, sy, U'├', c.line, c.bg);
  c.g.put(W - 1, sy, U'┤', c.line, c.bg);
  c.g.print(2, sy, "[ SCROLLS ]", c.hi, c.bg);
  for (int i = 0; i < 8; ++i) drawScrollSlot(c, 2 + (i % 4) * 14, sy + 1 + (i / 4) * 3, 14, i, i == scroll);
  const SampleScroll& s = kScrolls[static_cast<size_t>(scroll)];
  const auto desc = wrap(std::string(s.name) + " (MP " + std::to_string(s.mp) + "): " + (s.carried ? s.desc : "not carried"), c.chars(54));
  for (size_t i = 0; i < desc.size() && i < 2; ++i) c.text(3, sy + 7 + static_cast<int>(i) * 2, desc[i], s.carried ? c.fg : c.dim);
  const int ax = 57;
  c.g.print(ax - 1, sy, combat ? "[ ACTION ]" : "[ ACTIONS ]", c.hi, c.bg);
  drawActionGrid(c, ax, sy + 2, 13, sel, combat, 3);
  const int nx = W - 21;
  c.text(nx, sy + 2, "Gold 1234", c.th.coin);
  c.text(nx, sy + 4, "XP 380/412", c.dim);
  c.text(nx, sy + 6, "AC 6", c.fg);
  c.text(nx, sy + 8, "[Esc] Menu", c.dim);
}


// ---- the scrolls screen (shown when the player tries to cast) ------------------------------------------------------

// A modal over the view: the eight kinds on the left (the ones not carried stay listed but dim), the highlighted one's
// numbers and description on the right. Combat-only scrolls are dim out of combat.
void scrollsScreen(Ctx& c, int x, int y, int w, int h, int sel, bool combat) {
  c.clear(x, y, w, h);
  c.frame(x, y, w, h, "SCROLLS", c.hi);
  const int lw = std::min(36, w / 2);
  c.vrule(x + lw, y, y + h - 1);
  c.text(x + 2, y + 1, "You have 5 of 8 kinds", c.dim);
  c.textR(x + lw + 2, y + 1, w - lw - 4, "MP 11/20", c.th.mana);
  std::vector<int> held;  // only the scrolls the player carries are listed
  for (int i = 0; i < 8; ++i)
    if (kScrolls[static_cast<size_t>(i)].carried) held.push_back(i);
  if (std::find(held.begin(), held.end(), sel) == held.end()) sel = held.front();
  for (size_t k = 0; k < held.size(); ++k) {
    const int i = held[k];
    const SampleScroll& s = kScrolls[static_cast<size_t>(i)];
    const bool on = i == sel, usable = (combat || s.utility) && s.mp <= 11;
    const int ry = y + 4 + static_cast<int>(k) * 3;
    if (on) c.g.fill(x + 1, ry, lw - 1, 2, U' ', c.fg, c.th.selection);
    c.text(x + 2, ry, std::string(on ? ">" : " ") + s.name, on ? c.hi : usable ? c.fg : c.dim);
    c.textR(x + 2, ry, lw - 4, "MP " + std::to_string(s.mp), usable ? c.th.mana : c.dim);
  }
  const SampleScroll& s = kScrolls[static_cast<size_t>(sel)];
  const int dx = x + lw + 2, dw = w - lw - 4;
  c.text(dx, y + 4, s.name, c.hi);
  c.text(dx, y + 7, "MP " + std::to_string(s.mp) + "    Lv " + std::to_string(s.levelReq) + "    INT " + std::to_string(s.intReq), c.th.mana);
  c.text(dx, y + 9, s.utility ? "Use anywhere" : "Combat only", c.dim);
  const auto lines = wrap(s.desc, c.chars(dw));
  for (size_t i = 0; i < lines.size() && y + 12 + static_cast<int>(i) * 2 < y + h - 4; ++i) c.text(dx, y + 12 + static_cast<int>(i) * 2, lines[i], c.fg);
  c.text(dx, y + h - 3, combat || s.utility ? "[Enter] Cast    [Esc] Back" : "Not usable here    [Esc] Back", c.dim);
}

// ---- variant 4: log strip ------------------------------------------------------------------------------------------
// A full-width message log along the bottom (4 lines, ~70 characters each). One right-hand column: the minimap, or the action grid in a fight.

void variant4(Ctx& c, bool combat, int sel, int scroll, bool scrolls) {
  const int W = c.g.width(), H = c.g.height();
  const int rw = 36, vx = 1, vy = 1, vh = 32, vw = W - 3 - rw, ruleY = vy + vh, rx = vx + vw + 1;
  c.frame(0, 0, W, H, "LABYRINTH", c.line);
  c.g.print(W - 22, 0, "[ Dungeon 12 ]", c.hi, c.bg);
  c.hrule(0, W - 1, ruleY, "LOG");
  c.vrule(vx + vw, 0, ruleY);
  c.hint(W - 2, ruleY, "[S] Scrolls  [I] Inventory  [Esc] Menu");

  drawView(c, vx, vy, vw, vh, combat);
  if (combat) drawEnemyBar(c, vx, vy, vw);

  c.g.print(rx + 1, 1, combat ? "ACTION" : "MAP", c.hi, c.bg);
  if (combat) drawActionGrid(c, rx + (rw - 28) / 2, 3, 14, sel, true, 2);
  else drawMinimap(c, rx + (rw - 22) / 2, 3);
  drawHero(c, rx + 1, 14, rw - 2, true);
  c.text(rx + 1, 20, "Gold 1234", c.th.coin);
  c.text(rx + 1, 22, "XP 380/412", c.dim);
  c.g.print(rx + 1, 24, "CHARMEES", c.hi, c.bg);
  drawCharmee(c, rx + 1, 26, rw - 2, "Rat", 5, 7, 9, false);
  drawCharmee(c, rx + 1, 30, rw - 2, "Goblin", 8, 14, 20, false);

  SampleLog sl;
  drawLog(c, 2, ruleY + 2, W - 4, 4, combat ? sl.fightWide : sl.normalWide);
  if (scrolls) scrollsScreen(c, vx + 4, vy + 1, vw - 8, vh - 2, scroll, combat);
}

// ---- variant 5: context panel --------------------------------------------------------------------------------------
// The middle of the info row changes with the situation: the minimap while exploring, the action grid in a fight.

void variant5(Ctx& c, bool combat, int sel, int scroll, bool scrolls) {
  const int W = c.g.width(), H = c.g.height();
  const int vw = W - 2, vh = 28, vy = 1, r1 = vy + vh, r2 = r1 + 14;
  const int x1 = 38, x2 = 81;  // the two column rules
  c.frame(0, 0, W, H, "LABYRINTH", c.line);
  c.g.print(W - 22, 0, "[ Dungeon 12 ]", c.hi, c.bg);
  drawView(c, 1, vy, vw, vh, combat);
  if (combat) drawEnemyBar(c, 1, vy, vw);
  c.hrule(0, W - 1, r1, "STATUS");
  c.hrule(0, W - 1, r2, "LOG");
  c.vrule(x1, r1, r2);
  c.vrule(x2, r1, r2);
  c.hint(W - 2, r2, "[S] Scrolls  [I] Inventory  [Esc] Menu");

  drawHero(c, 2, r1 + 1, 36, true);
  c.text(2, r1 + 7, "STR12 DEX 9 INT14", c.fg);
  c.text(2, r1 + 9, "VIT11 CHA 8  AC 6", c.fg);
  c.text(2, r1 + 11, "Gold 1234  XP 380/412", c.dim);
  c.g.print(x1 + 2, r1, combat ? "[ ACTION ]" : "[ MAP ]", c.hi, c.bg);
  if (combat) drawActionGrid(c, x1 + 2, r1 + 4, 13, sel, true, 3);
  else drawMinimap(c, x1 + (x2 - x1 - 22) / 2, r1 + 2);
  c.g.print(x2 + 2, r1, "[ CHARMEES ]", c.hi, c.bg);
  drawCharmee(c, x2 + 2, r1 + 2, 34, "Rat", 5, 7, 9, true);
  drawCharmee(c, x2 + 2, r1 + 7, 34, "Goblin", 8, 14, 20, true);

  SampleLog sl;
  drawLog(c, 2, r2 + 1, W - 4, 4, combat ? sl.fightWide : sl.normalWide);
  if (scrolls) scrollsScreen(c, 5, vy + 1, vw - 8, vh - 2, scroll, combat);
}

// ---- variant 6: cinematic ------------------------------------------------------------------------------------------
// The view fills the window; only the character, charmees and minimap hover over it. The log has its own strip. In a fight the actions
// pop up over the lower right.

void variant6(Ctx& c, bool combat, int sel, int scroll, bool scrolls) {
  const int W = c.g.width(), H = c.g.height();
  const int vw = W - 2, vh = 36, vy = 1, ruleY = vy + vh;
  c.frame(0, 0, W, H, "LABYRINTH", c.line);
  c.g.print(W - 22, 0, "[ Dungeon 12 ]", c.hi, c.bg);
  drawView(c, 1, vy, vw, vh, combat);
  if (combat) drawEnemyBar(c, 1, vy, vw);
  const int top = combat ? 4 : 1;
  c.clear(2, top + 1, 31, 7);
  c.frame(2, top + 1, 31, 7, "", c.line);
  c.text(4, top + 2, "D. Crawley", c.hi);
  c.textR(4, top + 2, 27, "Lv 7", c.dim);
  c.text(4, top + 4, "HP 42/50", c.fg);
  c.bar(19, top + 4, 12, 0.84, c.th.good, mix(c.bg, c.th.good, 0.2));
  c.text(4, top + 6, "MP 11/20", c.fg);
  c.bar(19, top + 6, 12, 0.55, c.th.mana, mix(c.bg, c.th.mana, 0.2));
  c.clear(2, top + 8, 31, 8);
  c.frame(2, top + 8, 31, 8, "", c.line);
  drawCharmee(c, 4, top + 9, 27, "Rat", 5, 7, 9, false);
  drawCharmee(c, 4, top + 13, 27, "Goblin", 8, 14, 20, false);
  c.clear(W - 26, top + 1, 25, 13);
  c.frame(W - 26, top + 1, 25, 13, "", c.line);
  drawMinimap(c, W - 25, top + 2);
  if (combat) {
    c.clear(W - 31, vy + vh - 12, 30, 11);
    c.frame(W - 31, vy + vh - 12, 30, 11, "ACTION", c.line);
    drawActionGrid(c, W - 30, vy + vh - 11, 14, sel, true, 2);
  }
  c.hrule(0, W - 1, ruleY, "LOG");
  c.hint(W - 2, ruleY, "[S] Scrolls  [I] Inventory  [Esc] Menu");
  SampleLog sl;
  drawLog(c, 2, ruleY + 1, W - 4, 4, combat ? sl.fightWide : sl.normalWide);
  if (scrolls) scrollsScreen(c, 6, vy + 2, vw - 10, vh - 4, scroll, combat);
}

}  // namespace

int layoutVariantActionCols(int variant) { return variant == 1 || variant == 4 || variant == 6 ? 2 : 3; }

void layoutVariantGrid(int variant, int& cols, int& rows) {
  switch (variant) {
    case 2: cols = 120; rows = 60; break;  // 720 x 720 px at 6x12
    case 4: cols = 132; rows = 44; break;  // 792 x 528 px
    case 5: cols = 120; rows = 54; break;  // 720 x 648 px
    case 6: cols = 120; rows = 47; break;  // 720 x 564 px
    case 3: cols = 120; rows = 46; break;  // 720 x 552 px
    default: cols = 140; rows = 50; break;  // 840 x 600 px
  }
}

void drawLayoutVariant(CellGrid& g, const Theme& th, int variant, bool combat, int sel, int scroll, bool scrollsScreen, int lineStyle) {
  Ctx c{g, th, th.fg, th.muted, th.accent, th.bg, th.frame, th.selection};
  c.ln = kLineStyles[std::clamp(lineStyle, 0, 2)];
  g.clear(th.bg);
  sel = std::clamp(sel, 0, 5);
  scroll = std::clamp(scroll, 0, 7);
  switch (variant) {
    case 2: variant2(c, combat, sel, scroll); break;
    case 3: variant3(c, combat, sel, scroll); break;
    case 4: variant4(c, combat, sel, scroll, scrollsScreen); break;
    case 5: variant5(c, combat, sel, scroll, scrollsScreen); break;
    case 6: variant6(c, combat, sel, scroll, scrollsScreen); break;
    default: variant1(c, combat, sel, scroll); break;
  }
}

}  // namespace laby
