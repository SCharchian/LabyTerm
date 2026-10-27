// The main screen for a real terminal. Text is one cell wide and one row
// tall, so everything the SDL layout (MainScreen.cpp) spreads over 132 x 44 cells fits in 100 x 30 and grows with the
// terminal. It reads the same MainScreenOptions; pictures that the SDL screen draws as smooth images are drawn as
// ImageRuns too (the terminal backend folds them into octants, see render/TermFrame.cpp).
#include "ui/TermScreen.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "core/DataFile.hpp"
#include "core/Tuning.hpp"
#include "render/GlyphAtlas.hpp"
#include "ui/BossAura.hpp"
#include "ui/OrbArt.hpp"
#include "ui/PanelKit.hpp"
#include "ui/ScrollArt.hpp"
#include "ui/TermLayout.hpp"
#include "ui/TitleScreen.hpp"

namespace laby {
namespace {

int len(const std::string& s) {  // characters (the HUD text is ASCII; multi-byte bullets count by lead bytes)
  int n = 0;
  for (unsigned char ch : s) n += (ch & 0xC0) != 0x80;
  return n;
}
std::string clip(const std::string& s, int chars) { return len(s) <= chars ? s : s.substr(0, static_cast<size_t>(std::max(0, chars))); }

// Big letters: the glyph atlas at 6 x 12 pixels per letter (the SDL window's default cell size), drawn as octants, so each letter is 3 cells
// wide and 3 rows tall. Used where the SDL screen has large lettering (the enemy's bar, announcements).
const Atlas* bigAtlas() {
  static std::unique_ptr<Atlas> atlas;
  static bool tried = false;
  if (!tried) {
    tried = true;
    const auto fonts = findFonts(dataDir() + "/fonts");
    std::vector<FontFace> faces(fonts.size());
    std::vector<const FontFace*> chain;
    for (size_t i = 0; i < fonts.size(); ++i)
      if (faces[i].load(fonts[i].path)) chain.push_back(&faces[i]);
    if (!chain.empty()) atlas = std::make_unique<Atlas>(buildGlyphAtlas(6, 12, chain, 6));
  }
  return atlas.get();
}
constexpr int kBigCols = 3, kBigRows = 3;  // cells per big letter
int bigWidth(const std::string& t) { return kBigCols * len(t); }
// Draws `t` in big letters with its top-left cell at (x, y): `ink` on whatever cells are there; false (nothing drawn) without a font.
bool bigText(CellGrid& g, int x, int y, const std::string& t, Rgb ink, Rgb paper) {
  const Atlas* a = bigAtlas();
  if (!a || t.empty()) return false;
  const int n = static_cast<int>(t.size()), w = 6 * n, h = 12;
  auto img = std::make_shared<Image8>();
  img->w = w;
  img->h = h;
  img->tone.assign(static_cast<size_t>(w) * h, 255);
  img->alpha.assign(static_cast<size_t>(w) * h, 0);
  for (int i = 0; i < n; ++i) {
    const std::uint8_t* tile = a->tile(static_cast<unsigned char>(t[static_cast<size_t>(i)]));
    if (!tile) continue;
    for (int yy = 0; yy < h; ++yy)
      for (int xx = 0; xx < 6; ++xx) img->alpha[static_cast<size_t>(yy) * w + i * 6 + xx] = static_cast<std::uint8_t>(std::min(255, tile[yy * 6 + xx] * 3 / 2));  // thin strokes survive the 1-bit cut
  }
  ImageRun run;
  run.img = img;
  run.cx = x;
  run.cy = y;
  run.srcScale = 1;
  run.clipX0 = x;
  run.clipY0 = y;
  run.clipX1 = x + kBigCols * n;
  run.clipY1 = y + kBigRows;
  run.light = ink;
  run.dark = paper;
  g.addImage(run);
  return true;
}

// Text at (x, y) on cells of colour `bg`.
void text(CellGrid& g, int x, int y, const std::string& t, Rgb fg, Rgb bg) { g.print(x, y, t, fg, bg); }
void textRight(CellGrid& g, int x, int y, int w, const std::string& t, Rgb fg, Rgb bg) { g.print(x + std::max(0, w - len(t)), y, t, fg, bg); }
void textCentre(CellGrid& g, int x, int y, int w, const std::string& t, Rgb fg, Rgb bg) { g.print(x + std::max(0, (w - len(t)) / 2), y, t, fg, bg); }

// A bar `w` cells wide, filled from the left in eighths of a cell.
void bar(Ctx& c, int x, int y, int w, double frac, Rgb fill, Rgb empty) { c.bar(x, y, w, frac, fill, empty); }

// The bars' colours: health is the theme's red and mana its blue (the `good` and `mana` roles can both come out as the same teal).
// The mana blue is the theme's own when it is one (some themes' "blue" is orange), else a plain blue.
Rgb manaBlue(const Theme& th) { return th.blue.b >= th.blue.r + 30 && th.blue.b >= th.blue.g ? th.blue : Rgb{70, 130, 230}; }

// A label with its numbers on the right, then the bar under it: two rows.
void gauge(Ctx& c, int x, int y, int w, const std::string& label, int value, int maxValue, Rgb fill, bool low) {
  text(c.g, x, y, label, c.fg, c.bg);
  textRight(c.g, x, y, w, std::to_string(value) + "/" + std::to_string(maxValue), low ? c.th.danger : c.dim, c.bg);
  bar(c, x, y + 1, w, maxValue > 0 ? static_cast<double>(value) / maxValue : 0.0, fill, mix(c.bg, fill, 0.2));
}

// A solid highlight over a row of cells (a selected list row or button), in the select fill.
void highlight(Ctx& c, int x, int y, int w) { c.g.fill(x, y, w, 1, U' ', c.th.onGood, c.th.selectFill); }

// One action button, 3 rows: the outline, the label, the outline. The selected one is solid.
void button(Ctx& c, int bx, int by, int bw, const std::string& label, bool on, bool usable) {
  c.g.fill(bx, by, bw, 3, U' ', c.fg, c.bg);
  if (on) {
    c.g.fill(bx, by, bw, 3, U' ', c.th.onGood, c.th.selectFill);
    textCentre(c.g, bx, by + 1, bw, label, c.th.onGood, c.th.selectFill);
  } else {
    c.rect(bx, by, bw, 3, usable ? c.line : mix(c.bg, c.line, 0.4));
    textCentre(c.g, bx, by + 1, bw, label, usable ? c.fg : mix(c.bg, c.dim, 0.5), c.bg);
  }
}

// ---- the right column ----------------------------------------------------------------------------------------------------

void drawCharmee(Ctx& c, int x, int y, int w, const CharmeeInfo* m) {
  if (!m) {
    text(c.g, x, y, "(empty)", mix(c.bg, c.dim, 0.5), c.bg);
    bar(c, x, y + 1, w, 0, c.dim, mix(c.bg, c.dim, 0.15));
    return;
  }
  const Rgb fill = c.th.charmee;
  const double frac = m->maxHP > 0 ? static_cast<double>(std::max(0, m->hp)) / m->maxHP : 0.0;
  const std::string hp = std::to_string(std::max(0, m->hp)) + "/" + std::to_string(m->maxHP);
  text(c.g, x, y, clip(m->name + " L" + std::to_string(m->level), w - len(hp) - 1), c.fg, c.bg);
  textRight(c.g, x, y, w, hp, c.dim, c.bg);
  bar(c, x, y + 1, w, frac, fill, mix(c.bg, fill, 0.2));
}

void drawHero(Ctx& c, int x, int y, int w, const HudInfo& h) {
  const bool lost = h.hpHurt && h.hpBefore > h.hp && h.maxHP > 0;
  gauge(c, x, y, w, "HP", std::max(0, h.hp), h.maxHP, c.th.danger, lost || h.hp * 4 <= h.maxHP);
  if (lost) {  // the part just lost shows pale where the bar used to reach, until the next key press
    const Rgb pale = mix(c.th.danger, c.th.fg, 0.75);
    const int cells = std::clamp(static_cast<int>(std::ceil(static_cast<double>(w) * std::min(h.hpBefore, h.maxHP) / h.maxHP)), 1, w);
    const double keep = static_cast<double>(w) * std::max(0, h.hp) / h.maxHP / cells;  // (the red part, as a share of those cells)
    c.bar(x, y + 1, cells, keep, c.th.danger, pale);
  }
  gauge(c, x, y + 2, w, "MP", h.mp, h.maxMP, manaBlue(c.th), false);
  text(c.g, x, y + 4, "Gold", c.fg, c.bg);
  textRight(c.g, x, y + 4, w, std::to_string(h.gold), c.th.coin, c.bg);
  gauge(c, x, y + 5, w, "XP", h.xp, h.xpNeeded, c.dim, false);
}

// ---- the log ----------------------------------------------------------------------------------------------------------------

// The newest `lines` rows of the log, long messages wrapped; the newest message is bright. \x01 .. \x02 marks a monster's name (red).
void drawLog(Ctx& c, int x, int y, int w, int lines, const std::vector<std::string>& log) {
  struct Row { std::string text; bool first, newest, red; };
  std::vector<Row> rows;
  const int total = static_cast<int>(log.size()), width = std::max(1, w - 2);
  for (int i = std::max(0, total - lines); i < total; ++i) {
    bool red = false;
    bool first = true;
    for (const std::string& part : wrap(log[static_cast<size_t>(i)], width)) {
      rows.push_back({part, first, i == total - 1, red});
      first = false;
      for (char ch : part) {
        if (ch == '\x01') red = true;
        else if (ch == '\x02') red = false;
      }
    }
  }
  const int firstRow = std::max(0, static_cast<int>(rows.size()) - lines);
  for (int i = firstRow; i < static_cast<int>(rows.size()); ++i) {
    const Row& row = rows[static_cast<size_t>(i)];
    const int ry = y + i - firstRow;
    if (row.first) text(c.g, x, ry, ">", c.dim, c.bg);
    const Rgb base = row.newest ? c.hi : c.fg;
    bool red = row.red;
    int cx = x + 2;
    std::string seg;
    const auto flush = [&] {
      if (seg.empty()) return;
      cx = c.g.print(cx, ry, seg, red ? c.th.danger : base, c.bg);
      seg.clear();
    };
    for (char ch : row.text) {
      if (ch == '\x01' || ch == '\x02') { flush(); red = ch == '\x01'; }
      else seg += ch;
    }
    flush();
  }
}

// ---- panels over the view ---------------------------------------------------------------------------------------------------

// Rows of "left ........ right" in a panel, one row each, from y down to yEnd (exclusive).
void facts(Ctx& c, int x, int y, int w, int yEnd, const std::vector<OverlayLine>& lines) {
  for (const OverlayLine& l : lines) {
    if (y >= yEnd) break;
    const Rgb col = toneColor(c, l.tone), rcol = l.rightTone >= 0 ? toneColor(c, l.rightTone) : col;
    const int rw = len(l.right);
    text(c.g, x, y, clip(l.left, w - (rw ? rw + 1 : 0)), col, c.bg);
    if (rw) textRight(c.g, x, y, w, l.right, rcol, c.bg);
    ++y;
  }
}

int factsWidth(const std::vector<OverlayLine>& lines) {
  int w = 0;
  for (const OverlayLine& l : lines) w = std::max(w, len(l.left) + (l.right.empty() ? 0 : len(l.right) + 2));
  return w;
}

// The loot comparison, the fountain, the ESP box, the elevator and portal prompts.
void drawOverlay(Ctx& c, const OverlayBox& box, int vx, int vy, int vpCols, int vpRows, int top) {
  const int n = static_cast<int>(box.columns.size());
  const bool small = box.smallW > 0;
  const int gap = 1;
  // big panels are as wide as their words need (at most 46 cells each) and as tall as their lines, centred over the picture
  // (a reward's scroll or orb picture needs room: 10 rows of body, and a wider panel for the words beside it)
  const auto pictured = [](const OverlayColumn& col) { return col.orb != 0 || col.scroll >= 0; };
  const bool picture = !small && std::any_of(box.columns.begin(), box.columns.end(), pictured);
  const int maxColW = picture ? 56 : 46;
  const int colW = small ? 0 : std::min(maxColW, (vpCols - gap * (n - 1)) / std::max(1, n));
  int bigH = 0;
  for (const OverlayColumn& col : box.columns)
    bigH = std::max(bigH, 4 + (col.scroll >= 0 ? std::max(10, static_cast<int>(col.lines.size()) + 2) : static_cast<int>(col.lines.size()) + (col.orb ? 10 : 0)) +
                              (col.buttons.empty() && col.footer.empty() ? 0 : 2));
  bigH = std::min(bigH, vpRows);
  const int groupX = vx + (vpCols - (n * colW + gap * (n - 1))) / 2, groupY = vy + (vpRows - bigH) / 2;
  if (!small) {
    c.g.fill(groupX, groupY, n * colW + gap * (n - 1), bigH, U' ', c.fg, c.bg);  // (the gaps between the columns too)
    c.g.addTorchHole(groupX, groupY, n * colW + gap * (n - 1), bigH);
  }
  for (int ci = 0; ci < n; ++ci) {
    const OverlayColumn& col = box.columns[static_cast<size_t>(ci)];
    // a corner box is as big as its words
    int w = colW, h = bigH;
    const bool bigTitle = small && !col.bigTitle.empty() && bigAtlas();
    if (small) {
      w = std::max({len(col.title) + 6, factsWidth(col.lines) + 4, len(col.footer) + 4, 24});
      w = std::min(w, vpCols - 2);
      int buttonsW = 0;
      for (const std::string& b : col.buttons) buttonsW += len(b) + 4;
      w = std::max(w, buttonsW + 2);
      h = 2 + static_cast<int>(col.lines.size()) + (col.footer.empty() ? 0 : 1) + (col.buttons.empty() ? 0 : 2);
      if (bigTitle) {
        w = std::min(std::max({bigWidth(col.bigTitle) + 6, len(col.title) + 6, factsWidth(col.lines) + 4, 24}), vpCols - 2);
        h += kBigRows;
      }
      h = std::min(h, vpRows - top);
    }
    const int x0 = small ? vx + vpCols - w - 1 : groupX + ci * (colW + gap), y0 = small ? vy + top : groupY;
    c.g.fill(x0, y0, w, h, U' ', c.fg, c.bg);
    c.g.addTorchHole(x0, y0, w, h);
    c.rect(x0, y0, w, h, col.selected ? c.hi : c.line);
    if (!col.title.empty()) text(c.g, x0 + 2, y0, "[ " + col.title + " ]", col.selected ? c.hi : c.dim, c.bg);
    int y = y0 + 1 + (small ? 0 : 1);
    if (small && !col.bigTitle.empty() && bigAtlas()) {  // the name in big letters, centred, with the lines right under it
      bigText(c.g, x0 + (w - bigWidth(col.bigTitle)) / 2, y, col.bigTitle, col.selected ? c.hi : c.fg, c.bg);
      y += kBigRows;
    }
    if (pictured(col) && !small) {  // the picture, as the SDL build has it: the scroll at the left with its words beside it, or the orb with its words under it
      const int innerX = x0 + 3, innerW = w - 6, bodyEnd = y0 + h - 3;  // (the last body row; the footer is under it)
      if (col.scroll >= 0) {
        const int rowsAvail = bodyEnd + 1 - y, picRows = std::min(rowsAvail, 9), picCols = picRows * 2;
        if (picRows >= 8) drawScroll(c.g, innerX, y + (rowsAvail - picRows) / 2, picCols, picRows, col.scroll, scrollColors(c.th), col.scrollT);
        const int tx = innerX + picCols + 2, tw = innerW - picCols - 2, textRows = static_cast<int>(col.lines.size());
        int ty = y + std::max(0, (rowsAvail - textRows - 1) / 2);
        for (const OverlayLine& l : col.lines) {
          textCentre(c.g, tx, ty, tw, clip(l.left, tw), toneColor(c, l.tone), c.bg);
          ty += l.tone == 1 ? 2 : 1;  // the name gets a blank line under it
        }
      } else {
        const int textRows = static_cast<int>(col.lines.size()), orbRows = bodyEnd + 1 - textRows - y;
        // (health is the theme's red and mana its blue: the `good` and `mana` roles can both come out as the same teal)
        if (orbRows >= 8) drawOrb(c.g, innerX, y, innerW, orbRows, col.orb == 1 ? c.th.danger : manaBlue(c.th), c.bg, col.orbT);
        int ty = bodyEnd + 1 - textRows;
        for (const OverlayLine& l : col.lines) textCentre(c.g, innerX, ty++, innerW, clip(l.left, innerW), toneColor(c, l.tone), c.bg);
      }
    } else {
      facts(c, x0 + 2, y, w - 4, y0 + h - 1, col.lines);
    }
    if (!col.buttons.empty()) {  // side by side along the bottom
      int bx = x0 + 2;
      for (size_t i = 0; i < col.buttons.size(); ++i) {
        const bool on = static_cast<int>(i) == col.buttonSel;
        const std::string label = " " + col.buttons[i] + " ";
        c.g.fill(bx, y0 + h - 2, len(label) + 2, 1, U' ', on ? c.th.onGood : c.fg, on ? c.th.selectFill : c.bg);
        text(c.g, bx + 1, y0 + h - 2, label, on ? c.th.onGood : c.fg, on ? c.th.selectFill : c.bg);
        bx += len(label) + 4;
      }
    } else if (!col.footer.empty()) {
      textCentre(c.g, x0, y0 + h - 2, w, col.footer, col.selected ? c.hi : c.dim, c.bg);
    }
  }
}

// A sub-menu: Left = the scrolls, Wide = the inventory, Page = help text.
// Words and pictures on the town's picture, as the SDL build draws them (MainScreen.cpp), with text one row tall.
void drawTownScene(Ctx& c, const MainScreenOptions& opt, int vx, int vy, int vpCols, int vpRows) {
  CellGrid& g = c.g;
  if (opt.townSprites)
    for (const TownSprite& t : *opt.townSprites) {
      ImageRun run;
      run.img = t.img;
      run.cx = vx;
      run.cy = vy;
      run.srcScale = kSignDetail;
      run.sx = t.x;
      run.sy = t.y;
      run.clipX0 = vx;
      run.clipY0 = vy;
      run.clipX1 = vx + vpCols;
      run.clipY1 = vy + vpRows;
      run.light = t.lit ? c.hi : mix(c.bg, c.hi, 0.68);
      run.dark = c.bg;
      run.bilinear = true;
      g.addImage(run);
    }
  if (!opt.labels) return;
  for (const SceneLabel& l : *opt.labels) {
    if (l.cx < 0 || l.cy < 0 || l.cx + l.w > vpCols || l.cy + l.h > vpRows) continue;  // (a sign the picture shows only in part)
    const int x = vx + l.cx, y0 = vy + l.cy, y = y0 + l.h - 1;  // (y: the text's row)
    const int rw = len(l.right), room = l.w - (rw ? rw + 1 : 0);
    const Rgb paper = l.filled ? c.hi : c.bg;
    if (l.button) {
      const std::string t = l.filled ? "[ " + l.text + " ]" : "  " + l.text + "  ";
      g.fill(x, y, l.w, 1, U' ', c.fg, l.filled ? c.th.selectFill : c.bg);  // (buttons are one row tall)
      textCentre(g, x, y, l.w, t, l.filled ? c.th.onGood : c.fg, l.filled ? c.th.selectFill : c.bg);
      continue;
    }
    g.fill(x, y0, l.w, l.h, U' ', c.fg, paper);
    g.addTorchHole(x, y0, l.w, l.h);
    const std::string t = clip(l.text, room);
    const int tx = l.align == SceneLabel::Align::Left ? x : l.align == SceneLabel::Align::Right ? x + l.w - len(t) : x + (l.w - len(t)) / 2;
    Rgb col = l.filled ? c.bg : toneColor(c, l.tone);
    if (l.fade > 0) col = mix(paper, col, 1.0 - l.fade);
    text(g, tx, y, t, col, paper);
    if (rw) textRight(g, x, y, l.w, l.right, l.filled ? c.bg : toneColor(c, l.rightTone), paper);
  }
}

void drawMenu(Ctx& c, const MenuPanel& m, int vx, int vy, int vpCols, int vpRows, int inset) {
  using P = MenuPanel::Place;
  const bool page = m.place == P::Page, wide = m.place == P::Wide, dialog = m.place == P::Dialog, right = m.place == P::Right;
  const int availH = vpRows - inset;
  // a list panel is as tall as its rows and details (the footer sits in the bottom border); the big ones take the view, up to a size
  // that suits one-cell text, centred; a dialog is centred both ways, as wide as its words
  int dialogW = 44;
  if (dialog) {
    int need = len(m.title) + 8;
    for (const MenuRow& r : m.rows) need = std::max(need, len(r.left) + len(r.right) + 8);
    for (const OverlayLine& l : m.detail) need = std::max(need, len(l.left) + len(l.right) + 8);
    dialogW = std::clamp(need, 44, 64);
  }
  const int h = page ? availH : wide ? std::min(availH, 28) : std::min(availH, 3 + static_cast<int>(m.rows.size()) + static_cast<int>(m.detail.size()));
  const int w = page ? std::min(vpCols, 84) : wide ? std::min(vpCols, 104) : dialog ? std::min(vpCols, dialogW) : right ? std::min(vpCols, 34) : std::min(vpCols, 40);
  const int x = page || wide || dialog ? vx + (vpCols - w) / 2 : right ? vx + vpCols - w : vx;
  const int y = vy + inset + (dialog ? std::max(0, (availH - h) / 2) : 0);
  c.g.fill(x, y, w, h, U' ', c.fg, c.bg);
  c.g.addTorchHole(x, y, w, h);
  c.rect(x, y, w, h, c.hi);
  if (!m.title.empty()) text(c.g, x + 2, y, "[ " + m.title + " ]", c.hi, c.bg);
  const int listW = wide ? std::min(w / 2, m.listW > 0 ? std::min(m.listW, 46) : 40) : w - 2;
  const int pad = 1;
  int ry = y + 1;
  if (page) {
    int tx = x + 2;
    for (size_t i = 0; i < m.tabs.size(); ++i) {
      const bool on = static_cast<int>(i) == m.tab;
      if (on) highlight(c, tx - 1, ry, len(m.tabs[i]) + 2);
      text(c.g, tx, ry, m.tabs[i], on ? c.th.onGood : c.dim, on ? c.th.selectFill : c.bg);
      tx += len(m.tabs[i]) + 3;
    }
    ry += 1;
    for (int k = x + 1; k < x + w - 1; ++k) c.g.put(k, ry, c.ln.h, c.line, c.bg);
    ++ry;
    const int shown = y + h - 2 - ry, total = static_cast<int>(m.pageLines.size());
    const int first = std::clamp(m.scroll, 0, std::max(0, total - shown));
    std::vector<OverlayLine> lines(m.pageLines.begin() + first, m.pageLines.begin() + std::min(total, first + shown));
    facts(c, x + 2, ry, w - 4, y + h - 1, lines);
    if (first > 0) c.g.put(x + w - 3, y + 2, U'▲', c.hi, c.bg);
    if (first + shown < total) c.g.put(x + w - 3, y + h - 1, U'▼', c.hi, c.bg);
    if (!m.footer.empty()) text(c.g, x + 2, y + h - 1, " " + m.footer + " ", toneColor(c, m.footerTone), c.bg);
    return;
  }
  // the list (rows, each one line), the selected one solid; scrolled to keep it in view
  const int n = static_cast<int>(m.rows.size());
  const int detailRows = wide ? 0 : static_cast<int>(m.detail.size()) + 1;
  const int footRows = m.footer.empty() ? 0 : 2;
  // (a list panel: the top and bottom border, the rule over the details, then the details; the footer sits in the bottom border)
  const int listRows = wide ? std::max(1, std::min(n, h - 2 - detailRows - footRows - (static_cast<int>(m.extra.size()) + (m.extra.empty() ? 0 : 2))))
                            : std::max(1, std::min(n, h - 3 - static_cast<int>(m.detail.size())));
  const int first = n > listRows ? std::clamp(m.selected - listRows / 2, 0, n - listRows) : 0;
  for (int i = first; i < first + listRows && i < n; ++i, ++ry) {
    const MenuRow& r = m.rows[static_cast<size_t>(i)];
    const bool on = i == m.selected;
    const Rgb fg = on ? c.th.onGood : r.enabled ? c.fg : mix(c.bg, c.dim, 0.5), bg = on ? c.th.selectFill : c.bg;
    if (on) highlight(c, x + pad, ry, listW - 2 * pad + (wide ? 0 : 0));
    text(c.g, x + pad + 1, ry, clip(r.left, listW - 4 - len(r.right)), fg, bg);
    if (!r.right.empty()) textRight(c.g, x + pad + 1, ry, listW - 2 * pad - 2, r.right, on ? c.th.onGood : r.enabled ? toneColor(c, r.tone) : mix(c.bg, c.dim, 0.5), bg);
  }
  if (n > listRows) {
    if (first > 0) c.g.put(x + listW - 3, y, U'▲', c.hi, c.bg);
    if (first + listRows < n) c.g.put(x + listW - 3, ry, U'▼', c.hi, c.bg);
  }
  if (wide) {
    const int dx = x + listW;  // divider: the list at its left, the details at its right
    for (int yy = y + 1; yy < y + h - 1; ++yy) c.g.put(dx, yy, c.ln.v, c.line, c.bg);
    c.g.put(dx, y, c.ln.tt, c.line, c.bg);
    c.g.put(dx, y + h - 1, c.ln.bt, c.line, c.bg);
    if (!m.extra.empty()) {
      c.hrule(x, dx, ry, m.extraTitle);
      const int fw = listW - 2 * pad - 2;
      if (m.extraRight.empty()) {
        facts(c, x + pad + 1, ry + 1, fw, y + h - 1, m.extra);
      } else {
        const int colW = (fw - 2) / 2;
        facts(c, x + pad + 1, ry + 1, colW, y + h - 1, m.extra);
        facts(c, x + pad + 1 + colW + 2, ry + 1, colW, y + h - 1, m.extraRight);
      }
      if (!m.extraNote.empty()) {
        int nx = x + pad + 1;
        const int ny = ry + 1 + static_cast<int>(std::max(m.extra.size(), m.extraRight.size()));
        for (const OverlayLine& piece : m.extraNote) nx = c.g.print(nx, ny, piece.left, toneColor(c, piece.tone), c.bg);
      }
    }
    facts(c, dx + 2, y + 1, w - listW - 4, y + h - 2, m.detail);
    if (!m.footer.empty()) text(c.g, dx + 2, y + h - 1, " " + m.footer + " ", toneColor(c, m.footerTone), c.bg);
  } else {
    for (int k = x + 1; k < x + w - 1; ++k) c.g.put(k, ry, c.ln.h, c.line, c.bg);
    c.g.put(x, ry, c.ln.lt, c.line, c.bg);
    c.g.put(x + w - 1, ry, c.ln.rt, c.line, c.bg);
    facts(c, x + 2, ry + 1, w - 4, y + h - 1, m.detail);
    if (!m.footer.empty()) text(c.g, x + 2, y + h - 1, " " + clip(m.footer, w - 6) + " ", toneColor(c, m.footerTone), c.bg);
  }
}

// The whole floor over the view, with a key to its pictograms at the right.
void drawFullMap(Ctx& c, int vx, int vy, int vpCols, int vpRows, const MapPicture& map, int floorNo) {
  c.g.fill(vx, vy, vpCols, vpRows, U' ', c.fg, c.bg);
  c.g.addTorchHole(vx, vy, vpCols, vpRows);
  constexpr int kLegendCols = 22;
  const int size = std::clamp(std::min((vpCols - kLegendCols) * 2 / std::max(1, map.cols), vpRows * 4 / std::max(1, map.rows)), 2, 8);
  const int mw = mapCellsWide(map.cols, size), mh = mapCellsTall(map.rows, size);
  const int mx = vx + std::max(0, (vpCols - kLegendCols - mw) / 2), my = vy + std::max(0, (vpRows - mh) / 2);
  drawMapPicture(c.g, c.th, c.bg, mx, my, map, size);
  const int lx = std::min(mx + mw + 2, vx + vpCols - kLegendCols);
  text(c.g, lx, vy + 1, "FLOOR " + std::to_string(floorNo), c.hi, c.bg);
  struct Entry { MapTile tile; const char* name; };
  static const Entry kEntries[] = {{MapTile::Player, "You"},        {MapTile::DoorClosed, "Door"},   {MapTile::DoorOpen, "Open door"}, {MapTile::Fountain, "Fountain"},
                                   {MapTile::Elevator, "Elevator"}, {MapTile::Boss, "Boss"},         {MapTile::Chest, "Chest"}};
  int y = vy + 3;
  for (const Entry& e : kEntries) {
    if (y + 1 >= vy + vpRows - 2) break;
    MapPicture icon;
    icon.cols = icon.rows = 1;
    icon.tile = {e.tile};
    icon.facing = 0;
    drawMapPicture(c.g, c.th, c.bg, lx, y, icon, 4);  // 2 x 1 cells
    text(c.g, lx + 4, y, e.name, c.fg, c.bg);
    y += 2;
  }
  text(c.g, lx, vy + vpRows - 2, "[M/Esc] Close", c.dim, c.bg);
}

// ---- the fight ---------------------------------------------------------------------------------------------------------------

// The enemy's name and health across the top of the view: big letters over the bar when they fit (4 rows), else small ones (2 rows).
// Longer names give up " HP", then the level, before the letters go small.
struct BarText { bool big = false, bigRight = true; std::string left, right; };
BarText barText(const CombatHud& e, int vpCols) {
  const std::string hp = std::to_string(std::max(0, e.hp)) + "/" + std::to_string(e.maxHP);
  const std::string levelled = e.name + " L" + std::to_string(e.level);
  if (bigAtlas())
    for (const std::string& left : {levelled, e.name})
      for (const std::string& right : {hp + " HP", hp})
        if (bigWidth(left) + bigWidth(right) + 4 <= vpCols) return {true, true, left, right};
  // a long name: big, with the level and health small beside it
  const std::string small = "L" + std::to_string(e.level) + "  " + hp + " HP";
  if (bigAtlas() && bigWidth(e.name) + len(small) + 4 <= vpCols) return {true, false, e.name, small};
  return {false, false, levelled, hp + " HP"};
}
int barRowsFor(const CombatHud& e, int vpCols) { return barText(e, vpCols).big ? 4 : 2; }

void drawEnemyBar(Ctx& c, const CombatHud& e, int vx, int vy, int vpCols) {
  const Theme& th = c.th;
  const double frac = e.maxHP > 0 ? std::clamp(static_cast<double>(e.hp) / e.maxHP, 0.0, 1.0) : 0.0;
  const Rgb fill = th.gauge, slot = mix(c.bg, fill, 0.14);
  const BarText bt = barText(e, vpCols);
  const std::string &left = bt.left, &right = bt.right;
  const bool big = bt.big;
  const int rows = big ? 4 : 2, barY = vy + rows - 1;
  c.g.fill(vx, vy, vpCols, rows - 1, U' ', c.fg, c.bg);
  if (big) {
    bigText(c.g, vx + 1, vy, left, c.fg, c.bg);
    if (bt.bigRight) bigText(c.g, vx + vpCols - 1 - bigWidth(right), vy, right, c.fg, c.bg);
    else textRight(c.g, vx, vy + 1, vpCols - 1, right, c.fg, c.bg);
  } else {
    text(c.g, vx + 1, vy, clip(left, vpCols - len(right) - 4), c.fg, c.bg);
    textRight(c.g, vx, vy, vpCols - 1, right, c.fg, c.bg);
  }
  const double fillCells = frac * vpCols;
  static const char32_t kPartial[] = {U' ', U'\u258F', U'\u258E', U'\u258D', U'\u258C', U'\u258B', U'\u258A', U'\u2589'};
  for (int x = 0; x < vpCols; ++x) {
    const double left_ = fillCells - x;
    if (left_ >= 1.0) c.g.put(vx + x, barY, U'\u2588', fill, c.bg);
    else if (left_ > 0.0) c.g.put(vx + x, barY, kPartial[std::clamp(static_cast<int>(left_ * 8), 0, 7)], fill, slot);
    else c.g.put(vx + x, barY, U' ', fill, slot);
  }
}

// The monster, on the view's floor with a shadow under it, as big as the view allows.
void drawMonster(CellGrid& g, const Theme& th, const CombatHud& e, int vx, int vy, int vpCols, int vpRows, int menuShift, int kBarRows) {
  const auto img = e.image;
  if (!img || img->empty()) return;
  const Rgb bg = th.bg, hi = th.accent;
  const bool big = e.boss10;
  constexpr int kFloorGap = 3;  // sub-pixels from the view's bottom to the feet
  float kScale = 0.88f;
  if (big) kScale = std::max(kScale, kScale * static_cast<float>(tuning().monsters.boss10Scale));
  int footRow = 0;
  for (int y = img->h - 1; y >= 0 && !footRow; --y)
    for (int x = 0; x < img->w; ++x)
      if (img->alpha[static_cast<size_t>(y) * img->w + x] > 128) { footRow = y + 1; break; }
  int fx0 = img->w, fx1 = 0;
  for (int y = std::max(0, footRow - 8); y < footRow; ++y)
    for (int x = 0; x < img->w; ++x)
      if (img->alpha[static_cast<size_t>(y) * img->w + x] > 128) { fx0 = std::min(fx0, x); fx1 = std::max(fx1, x + 1); }
  if (fx1 <= fx0) { fx0 = 0; fx1 = img->w; }
  // fit the picture into what the view leaves: its height under the bar, its width beside any menu
  const float fitH = static_cast<float>((vpRows - kBarRows) * 4 - kFloorGap - 2) * 2.0f / static_cast<float>(std::max(1, img->h));
  const float fitW = static_cast<float>((vpCols - menuShift) * 2 - 2) * 2.0f / static_cast<float>(std::max(1, img->w));
  kScale = std::min({kScale, fitH, fitW});
  const float toSub = kScale / 2.0f;  // image pixel -> sub-pixel (the art is 2x)
  const int sx0 = static_cast<int>(std::lround((vpCols * 2 + menuShift * 2 - img->w * toSub) / 2));
  int floorY = vpRows * 4 - kFloorGap;
  const int lift = static_cast<int>(std::lround(e.hover * footRow * toSub));
  int sy0 = floorY - lift - static_cast<int>(std::lround(footRow * toSub));
  if (sy0 < kBarRows * 4) {
    const int need = kBarRows * 4 - sy0, down = std::min(need, lift);
    sy0 += down;
    if (need > down) floorY += need - down;
    sy0 = std::max(sy0, kBarRows * 4);
  }
  {  // contact shadow
    const float footW = (e.hover > 0 ? img->w * 0.55f : (fx1 - fx0) * 1.3f) * toSub;
    const int sw = std::max(6, static_cast<int>(std::lround(footW))), sh = 3;
    auto shadow = std::make_shared<Image8>();
    shadow->w = sw;
    shadow->h = sh;
    shadow->tone.assign(static_cast<size_t>(sw) * sh, 0);
    shadow->alpha.assign(static_cast<size_t>(sw) * sh, 0);
    for (int y = 0; y < sh; ++y)
      for (int x = 0; x < sw; ++x) {
        const float u = (x + 0.5f) / sw * 2 - 1, v = (y + 0.5f) / sh * 2 - 1;
        if (u * u + v * v <= 1.0f) shadow->alpha[static_cast<size_t>(y) * sw + x] = 255;
      }
    ImageRun sr;
    sr.img = shadow;
    sr.cx = vx;
    sr.cy = vy;
    sr.sx = sx0 + static_cast<int>(std::lround((e.hover > 0 ? img->w : fx0 + fx1) * toSub / 2)) - sw / 2;
    sr.sy = floorY - sh / 2;
    sr.clipX0 = vx;
    sr.clipY0 = vy + kBarRows;
    sr.clipX1 = vx + vpCols;
    sr.clipY1 = vy + vpRows;
    sr.srcScale = 1;
    sr.light = sr.dark = mix(bg, hi, 0.1);
    g.addImage(sr);
  }
  if (big) {  // the glow: two rings round the silhouette, red, breathing (the outer one solid here: a dither is below what a cell can show)
    const Aura aura = auraFor(img, false);
    for (int ring = 0; ring < 2; ++ring) {
      ImageRun ar;
      ar.img = ring == 0 ? aura.outer : aura.inner;
      ar.cx = vx;
      ar.cy = vy;
      ar.srcScale = 2;
      ar.scale = kScale;
      ar.sx = sx0 - static_cast<int>(std::lround(aura.pad * toSub));
      ar.sy = sy0 - static_cast<int>(std::lround(aura.pad * toSub));
      ar.clipX0 = vx;
      ar.clipY0 = vy + kBarRows;
      ar.clipX1 = vx + vpCols;
      ar.clipY1 = vy + vpRows;
      ar.light = ar.dark = mix(bg, th.danger, ring == 0 ? 0.22 + 0.16 * e.pulse : 0.42 + 0.30 * e.pulse);
      g.addImage(ar);
    }
  }
  ImageRun run;
  run.img = img;
  run.cx = vx;
  run.cy = vy;
  run.srcScale = 2;
  run.scale = kScale;
  run.sx = sx0;
  run.sy = sy0;
  run.clipX0 = vx;
  run.clipY0 = vy + kBarRows;
  run.clipX1 = vx + vpCols;
  run.clipY1 = vy + vpRows;
  run.light = big ? mix(hi, th.danger, 0.7) : hi;
  run.dark = big ? mix(bg, th.danger, 0.3) : mix(bg, hi, 0.2);  // (lighter than the SDL screen's: a cell has only two colours, and the darkest tone would vanish into the view)
  g.addImage(run);
}

}  // namespace

void drawMainScreenTerm(CellGrid& g, const Theme& th, const MainScreenOptions& opt) {
  const TermLayout L = termLayout(g.width(), g.height());
  const int W = L.W, H = L.H, vx = L.vx, vy = L.vy, vpCols = L.vpCols, vpRows = L.vpRows;
  const Rgb fg = th.fg, bg = th.bg, hi = th.accent;
  g.clear(bg);
  Ctx c{g, th, fg, th.muted, hi, bg, th.frame, th.selection};
  c.ln = kLineStyles[2];  // double lines

  // ---- outlines ----
  c.frame(0, 0, W, H, "LABYRINTH", c.line);
  c.hrule(0, W - 1, L.ruleY, "LOG");
  c.vrule(L.ruleX, 0, L.ruleY);
  if (opt.hud && !opt.hud->place.empty()) {
    const std::string place = "[ " + opt.hud->place + " ]";
    g.print(L.ruleX - 2 - len(place), 0, place, hi, bg);
  }
  if (!opt.hints.empty()) c.hint(W - 2, L.ruleY, opt.hints);

  // ---- the first-person view ----
  if (opt.view) {
    const Rgb glow = mix(hi, th.danger, 0.55 + 0.35 * opt.bossGlowPulse);
    const ObjectColors oc = objectColors(th);
    drawViewLayers(g, vx, vy, *opt.view, hi, bg, opt.bossGlow ? &glow : nullptr, &oc);
  }
  if (!(opt.menu || (opt.overlay && !opt.overlay->columns.empty()))) drawTownScene(c, opt, vx, vy, vpCols, vpRows);  // (drawn above the cells, so not under a panel)
  g.setViewport(vx, vy, vx + vpCols, vy + vpRows);

  const int kBarRows = opt.combat ? barRowsFor(*opt.combat, vpCols) : 0;
  if (opt.torch) {  // the torch's light on the picture, below the enemy's bar (the bar is UI); the terminal frame applies it to the cells (render/TermFrame.cpp)
    CellGrid::Torch t;
    t.on = true;
    t.rect = {vx, vy + kBarRows, vx + vpCols, vy + vpRows};
    const double m = tuning().torch.darkColor;
    t.dark = Rgb{static_cast<std::uint8_t>(bg.r * m), static_cast<std::uint8_t>(bg.g * m), static_cast<std::uint8_t>(bg.b * m)};
    t.shift = opt.torchShift;
    t.pulse = opt.torchPulse;
    t.breath = opt.torchBreath;
    g.setTorch(t);
  }
  // a panel over the whole picture hides the monster (its picture is drawn above the cells, so it would show through)
  const bool bigPanel = (opt.overlay && !opt.overlay->columns.empty() && opt.overlay->smallW == 0) || (opt.menu && opt.menu->place != MenuPanel::Place::Left) || opt.fullMap;
  if (opt.combat) {
    const CombatHud& e = *opt.combat;
    drawEnemyBar(c, e, vx, vy, vpCols);
    if (e.image && !e.image->empty() && !bigPanel) {
      const int menuShift = opt.menu && opt.menu->place == MenuPanel::Place::Left ? std::min(vpCols, 40) + 1 : 0;
      drawMonster(g, th, e, vx, vy, vpCols, vpRows, menuShift, kBarRows);
    }
    if (!e.banner.empty()) {  // a special action, or the death banner: a flash band across the view, over the monster
      const Rgb tone = e.bannerKind == 3 ? th.coin : e.bannerKind == 4 ? th.mana : e.bannerKind == 5 ? th.accent : th.danger;
      const double flash = e.bannerT < 0.3 ? 0.65 - e.bannerT : 0.35;
      const Rgb band = mix(bg, tone, std::clamp(flash, 0.2, 0.65));
      const bool bigBanner = bigWidth(e.banner) + 2 <= vpCols && bigAtlas();
      const int row = vy + kBarRows + 1, titleRows = bigBanner ? kBigRows : 1, rows = titleRows + (e.bannerHint.empty() ? 0 : 2);
      g.fill(vx, row, vpCols, rows, U' ', fg, band);
      g.addTorchHole(vx, row, vpCols, rows);  // (the torch must not dim it)
      auto cover = std::make_shared<Image8>();  // (the monster's picture is drawn over the cells: this opaque picture hides it under the band)
      cover->w = vpCols * 2;
      cover->h = rows * 4;
      cover->tone.assign(static_cast<size_t>(cover->w) * cover->h, 0);
      cover->alpha.assign(static_cast<size_t>(cover->w) * cover->h, 255);
      ImageRun hide;
      hide.img = cover;
      hide.cx = vx;
      hide.cy = row;
      hide.srcScale = 1;
      hide.clipX0 = vx;
      hide.clipY0 = row;
      hide.clipX1 = vx + vpCols;
      hide.clipY1 = row + rows;
      hide.light = hide.dark = band;
      g.addImage(hide);
      if (bigBanner) bigText(g, vx + (vpCols - bigWidth(e.banner)) / 2, row, e.banner, bg, band);
      else g.printText(vx + std::max(0, (vpCols - len(e.banner)) / 2), row, 0, 0, e.banner, bg);
      if (!e.bannerHint.empty()) {
        const double breathe = 0.5 + 0.5 * std::sin(e.bannerT * 4.0);
        g.printText(vx + std::max(0, (vpCols - len(e.bannerHint)) / 2), row + titleRows + 1, 0, 0, e.bannerHint, mix(band, fg, 0.55 + 0.45 * breathe));
      }
    }
    if (!e.status.empty()) {
      const std::string t = " " + e.status + " ";
      g.addTorchHole(vx + 1, vy + vpRows - 1, len(t), 1);
      g.print(vx + 1, vy + vpRows - 1, t, th.danger, th.accent);
    }
  }

  // ---- right column ----
  const bool fighting = opt.combat != nullptr;
  g.print(L.rx + 1, 0, fighting ? "[ ACTION ]" : opt.guide ? "[ TOWN ]" : "[ MAP ]", hi, bg);
  if (fighting && !opt.combat->over) {
    static const char* kActionName[6] = {"Attack", "Charm", "Scroll", "Hide", "Trick", "Pray"};
    const int bw = L.rw / 2;
    for (int i = 0; i < 6; ++i)
      button(c, L.rx + (i % 2) * bw, 1 + (i / 2) * 3, bw, kActionName[i], i == opt.combat->selected, opt.combat->enabled[static_cast<size_t>(i)]);
  } else if (fighting) {
    text(g, L.rx + 2, 3, "YOU DIED", th.danger, bg);
    text(g, L.rx + 2, 5, "[Enter/Z] Continue", fg, bg);
  } else if (opt.guide) {
    facts(c, L.rx + 1, 2, L.rw - 2, 10, *opt.guide);
  } else if (opt.minimap) {
    constexpr int kTile = 4;  // 7 tiles of 4 sub-pixels: 14 x 7 cells
    drawMapPicture(g, th, bg, L.rx + (L.rw - mapCellsWide(kMinimapSize, kTile)) / 2, 1 + (9 - mapCellsTall(kMinimapSize, kTile)) / 2, *opt.minimap, kTile);
  }
  if (const HudInfo* h = opt.hud) {
    c.hrule(L.ruleX, W - 1, 10, h->name + "  Lv " + std::to_string(h->level));
    drawHero(c, L.rx + 1, 11, L.rw - 2, *h);
    c.hrule(L.ruleX, W - 1, 18, "CHARMEES");
    for (int i = 0; i < 2; ++i)
      drawCharmee(c, L.rx + 1, 19 + i * 2, L.rw - 2, static_cast<size_t>(i) < h->charmees.size() ? &h->charmees[static_cast<size_t>(i)] : nullptr);
  }

  // ---- log ----
  if (opt.log) drawLog(c, 2, L.ruleY + 1, W - 4, L.logLines, *opt.log);

  const int top = kBarRows;
  if (opt.overlay && !opt.overlay->columns.empty()) drawOverlay(c, *opt.overlay, vx, vy, vpCols, vpRows, top + 1);
  if (opt.fullMap) drawFullMap(c, vx, vy, vpCols, vpRows, *opt.fullMap, opt.floorNumber);
  if (opt.menu) drawMenu(c, *opt.menu, vx, vy, vpCols, vpRows, top);
  if (!opt.bottomHints.empty()) g.print(2, H - 1, opt.bottomHints, c.dim, bg);
  if (!opt.statusTag.empty()) {
    const std::string tag = "[ " + opt.statusTag + " ]";
    g.print(std::max(1, W - 2 - len(tag)), H - 1, tag, c.dim, bg);
  }
}

// ---- the screens outside the dungeon ----------------------------------------------------------------------------------------

// The system menu (Options, Help, About) over a rectangle of the screen.
void drawMenuOverTerm(CellGrid& g, const Theme& th, const MenuPanel& menu, int x, int y, int cols, int rows) {
  Ctx c{g, th, th.fg, th.muted, th.accent, th.bg, th.frame, th.selection};
  c.ln = kLineStyles[2];
  drawMenu(c, menu, x, y, cols, rows, 0);
}

namespace {
Ctx titleCtx(CellGrid& g, const Theme& th) {
  Ctx c{g, th, th.fg, th.muted, th.accent, th.bg, th.frame, th.selection};
  c.ln = kLineStyles[2];
  return c;
}
}  // namespace

void drawTitleScreenTerm(CellGrid& g, const Theme& th, std::uint64_t ms, const std::vector<std::string>& rows, int selected, int bigRow, const std::string& otherWay) {
  const bool flash = drawStorm(g, th, ms);
  Ctx c = titleCtx(g, th);
  const Rgb dimText = flash ? th.bg : th.muted, textBg = flash ? mix(th.bg, th.accent, 0.45) : th.bg;
  text(g, 3, g.height() - 3, "A game by Sam Charchian", flash ? th.bg : c.hi, textBg);
  if (!otherWay.empty()) textRight(g, 0, 1, g.width() - 3, otherWay, flash ? th.bg : th.mana, textBg);  // top right, quiet
  // the choices, low and at the right so the picture shows; each is a bar as wide as its text; the first is set apart
  constexpr int kW = 28;
  const int x = g.width() - kW - 3;
  int y = g.height() - 3 - 2 * static_cast<int>(rows.size()) - 1;
  for (size_t i = 0; i < rows.size(); ++i, y += 2) {
    const bool on = static_cast<int>(i) == selected, big = static_cast<int>(i) == bigRow;
    const int barW = std::min(kW, len(rows[i]) + 6), barX = x + (kW - barW) / 2;
    const Rgb fg = on ? th.onGood : big ? c.hi : c.fg, bg = on ? th.selectFill : textBg;
    if (on) g.fill(barX, y, barW, 1, U' ', fg, bg);
    text(g, x + (kW - len(rows[i])) / 2, y, rows[i], fg, bg);
  }
  const std::string hint = "[Arrows] Choose  [Enter/Z] Select  [Esc] Menu";
  text(g, 3, g.height() - 1, hint, dimText, textBg);
  const std::string version = "v" LABYTERM_VERSION;
  text(g, g.width() - 3 - len(version), g.height() - 1, version, dimText, textBg);
}

void drawCreationScreenTerm(CellGrid& g, const Theme& th, std::uint64_t ms, const CreationInfo& info) {
  drawStorm(g, th, ms, false);
  Ctx c = titleCtx(g, th);
  const int W = std::min(g.width() - 4, 76), H = 23, x = (g.width() - W) / 2, y = std::max(1, (g.height() - H) / 2);
  g.fill(x, y, W, H, U' ', c.fg, c.bg);
  c.frame(x, y, W, H, "NEW CHARACTER", c.hi);
  const int left = x + 3, inner = W - 6;
  const auto row = [&](int selectedId, int ry, const std::string& label, bool centre) {
    const bool on = info.selected == selectedId;
    if (on) g.fill(left - 1, ry, inner + 2, 1, U' ', th.onGood, th.selectFill);
    text(g, centre ? left + (inner - len(label)) / 2 : left, ry, label, on ? th.onGood : c.fg, on ? th.selectFill : c.bg);
  };
  {  // Name: the label and a boxed field
    const bool on = info.selected == 0;
    const int by = y + 2, fieldX = left + 8, fieldW = 30;
    text(g, left, by, "Name", on ? th.onGood : c.fg, on ? th.selectFill : c.bg);
    const Rgb edge = info.editing ? c.hi : on ? mix(c.line, c.hi, 0.6) : c.line;
    c.rect(fieldX - 1, by - 1, fieldW + 2, 3, edge);
    g.fill(fieldX, by, fieldW, 1, U' ', c.fg, c.bg);
    text(g, fieldX + 1, by, info.name + (info.editing ? "_" : ""), info.editing || on ? c.hi : c.fg, c.bg);
  }
  const int colW = (inner - 4) / 2, rightX = left + colW + 4;
  text(g, left, y + 5, "Your stats", c.dim, c.bg);
  text(g, rightX, y + 5, info.itemTitle, c.dim, c.bg);
  for (int k = left; k < left + colW; ++k) g.put(k, y + 6, c.ln.h, c.line, c.bg);
  for (int k = rightX; k < rightX + colW; ++k) g.put(k, y + 6, c.ln.h, c.line, c.bg);
  facts(c, left, y + 7, colW, y + 17, info.statLines);
  facts(c, rightX, y + 7, colW, y + 17, info.itemLines);
  row(1, y + H - 5, "Re-Roll Character", false);
  row(2, y + H - 3, "Enter the Labyrinth!", true);
  const std::string hint = info.editing ? "Type a name  [Enter] Done  [Bksp] Delete" : "[Arrows] Choose  [Enter/Z] Select  [Esc] Menu";
  text(g, (g.width() - len(hint)) / 2, g.height() - 1, hint, c.dim, c.bg);
}

void drawLegendsScreenTerm(CellGrid& g, const Theme& th, std::uint64_t ms, const LegendsInfo& info) {
  const bool flash = drawStorm(g, th, ms, false);
  Ctx c = titleCtx(g, th);
  const Rgb headBg = flash ? mix(th.bg, th.accent, 0.45) : th.bg;
  const int W = std::min(g.width() - 4, 64), H = 17, x = (g.width() - W) / 2, y = std::max(3, (g.height() - H) / 2 + 1);
  textCentre(g, 0, y - 2, g.width(), "L E G E N D S", flash ? th.bg : th.accent, headBg);
  g.fill(x, y, W, H, U' ', c.fg, c.bg);
  c.frame(x, y, W, H, "TOP 10", c.hi);
  const int left = x + 3, right = x + W - 4, nameX = left + 6, scoreW = 9, floorW = 5;
  const auto cols = [&](int ry, const std::string& rank, const std::string& name, const std::string& fl, const std::string& sc, Rgb ink) {
    text(g, left, ry, rank, ink, c.bg);
    text(g, nameX, ry, name, ink, c.bg);
    textRight(g, right - scoreW - 2 - floorW, ry, floorW, fl, ink, c.bg);
    textRight(g, right - scoreW, ry, scoreW, sc, ink, c.bg);
  };
  cols(y + 1, "RANK", "NAME", "FLOOR", "SCORE", c.dim);
  for (int k = left; k < right; ++k) g.put(k, y + 2, c.ln.h, c.line, c.bg);
  static const char* kOrdinal[] = {"1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH", "9TH", "10TH"};
  const bool blinkOn = (ms / 450) % 2 == 0;
  for (int i = 0; i < 10; ++i) {
    const bool have = i < static_cast<int>(info.rows.size()), mine = info.highlight == i + 1;
    const Rgb ink = mine ? (blinkOn ? c.hi : c.dim) : have ? (i == 0 ? th.accent : c.fg) : c.dim;
    if (!have) {
      cols(y + 3 + i, kOrdinal[i], "............", "", "", ink);
      continue;
    }
    const auto& r = info.rows[static_cast<size_t>(i)];
    cols(y + 3 + i, kOrdinal[i], r.name.substr(0, 14), std::to_string(r.floor), std::to_string(r.score), ink);
  }
  const std::string hint = info.fromGameOver ? "[Enter/Z] Continue  [Esc] Title" : "[Enter/Z] Back  [Esc] Menu";
  text(g, (g.width() - len(hint)) / 2, g.height() - 1, hint, c.dim, headBg);
}

void drawGameOverScreenTerm(CellGrid& g, const Theme& th, std::uint64_t ms, const GameOverInfo& info) {
  const bool flash = drawStorm(g, th, ms, false);
  Ctx c = titleCtx(g, th);
  const Rgb headBg = flash ? mix(th.bg, th.danger, 0.45) : th.bg;
  const int W = std::min(g.width() - 4, 56), H = 21, x = (g.width() - W) / 2, y = std::max(3, (g.height() - H) / 2 + 1);
  textCentre(g, 0, y - 2, g.width(), "G A M E   O V E R", flash ? th.bg : th.danger, headBg);
  g.fill(x, y, W, H, U' ', c.fg, c.bg);
  c.frame(x, y, W, H, info.name.empty() ? "YOU DIED" : info.name, c.hi);
  const int left = x + 3, inner = W - 6;
  facts(c, left, y + 2, inner, y + 9, info.facts);
  for (int k = left; k < left + inner; ++k) g.put(k, y + 9, c.ln.h, c.line, c.bg);
  facts(c, left, y + 11, inner, y + H - 4, info.score);
  g.fill(left - 1, y + H - 3, inner + 2, 1, U' ', th.onGood, th.selectFill);
  textCentre(g, left, y + H - 3, inner, "Continue", th.onGood, th.selectFill);
  const std::string hint = "[Enter/Z] Continue  [Esc] Title";
  text(g, (g.width() - len(hint)) / 2, g.height() - 1, hint, c.dim, headBg);
}

}  // namespace laby
