#include "ui/MainScreen.hpp"
#include "ui/OrbArt.hpp"
#include "ui/ScrollArt.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "core/Tuning.hpp"
#include "ui/PanelKit.hpp"
#include "ui/ScreenLayout.hpp"

namespace laby {
namespace {

// A Boss10's glow: two rings round the picture's silhouette (a solid one, then a dithered one), as images the size of the picture plus a margin.
// Built once per picture. Pixels are 2x-art pixels, like the picture's own.
struct Aura {
  std::shared_ptr<const Image8> inner, outer;
  int pad = 0;
};
Aura auraFor(const std::shared_ptr<const Image8>& img) {
  static std::vector<std::pair<const Image8*, Aura>> cache;
  for (const auto& e : cache)
    if (e.first == img.get()) return e.second;
  constexpr int kPad = 22, kInner = 9, kOuter = 21;
  const int W = img->w + 2 * kPad, H = img->h + 2 * kPad;
  // chamfer distance (3-4) from the silhouette
  std::vector<int> d(static_cast<size_t>(W) * H, 1 << 20);
  for (int y = 0; y < img->h; ++y)
    for (int x = 0; x < img->w; ++x)
      if (img->alpha[static_cast<size_t>(y) * img->w + x] > 128) d[static_cast<size_t>(y + kPad) * W + x + kPad] = 0;
  const auto at = [&](int x, int y) -> int& { return d[static_cast<size_t>(y) * W + x]; };
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      int v = at(x, y);
      if (x > 0) v = std::min(v, at(x - 1, y) + 3);
      if (y > 0) v = std::min(v, at(x, y - 1) + 3);
      if (x > 0 && y > 0) v = std::min(v, at(x - 1, y - 1) + 4);
      if (x + 1 < W && y > 0) v = std::min(v, at(x + 1, y - 1) + 4);
      at(x, y) = v;
    }
  for (int y = H - 1; y >= 0; --y)
    for (int x = W - 1; x >= 0; --x) {
      int v = at(x, y);
      if (x + 1 < W) v = std::min(v, at(x + 1, y) + 3);
      if (y + 1 < H) v = std::min(v, at(x, y + 1) + 3);
      if (x + 1 < W && y + 1 < H) v = std::min(v, at(x + 1, y + 1) + 4);
      if (x > 0 && y + 1 < H) v = std::min(v, at(x - 1, y + 1) + 4);
      at(x, y) = v;
    }
  const auto make = [&](int lo, int hi, bool dither) {
    auto m = std::make_shared<Image8>();
    m->w = W;
    m->h = H;
    m->tone.assign(static_cast<size_t>(W) * H, 0);
    m->alpha.assign(static_cast<size_t>(W) * H, 0);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const int dist = at(x, y) / 3;
        if (dist > lo && dist <= hi && (!dither || (x / 2 + y / 2) % 2 == 0)) m->alpha[static_cast<size_t>(y) * W + x] = 255;
      }
    return m;
  };
  Aura a{make(0, kInner, false), make(kInner, kOuter, true), kPad};
  cache.emplace_back(img.get(), a);
  return a;
}

// The holder at each end of the health bar, drawn in code: a plain bevelled ring (a half circle at the outer end that runs
// straight on, as wide as the bar's rails, to the edge of the picture), lit from the top left. Inside the ring is a transparent
// half-circle window (everything else is opaque, the corners in the background colour), so the bar underneath shows through
// with a perfectly round end, a thin dark gap round it.
// 36 x 48 pixels, which is 6 x 4 cells at the default 6 x 12 cell size (3 pixels per sub-pixel). `right` mirrors it but keeps
// the light on the top left. Tone 0 is the darkest metal, 255 the brightest.
std::shared_ptr<const Image8> barCapImage(bool right) {
  static std::shared_ptr<const Image8> cache[2];
  if (cache[right]) return cache[right];
  constexpr int W = 36, H = 48;
  constexpr double kPi = 3.14159265358979;
  constexpr double cxs = 24.0, cys = H / 2.0;      // the centre of the round end
  constexpr double kOuter = 24.0, kRing = 19.0, kWindow = 18.0;  // outer edge of the ring, inner edge of the ring, the window (the bar's end)
  auto img = std::make_shared<Image8>();
  img->w = W;
  img->h = H;
  img->tone.assign(static_cast<size_t>(W) * H, 0);
  img->alpha.assign(static_cast<size_t>(W) * H, 0);
  const double lx = right ? 0.7071 : -0.7071, ly = -0.7071;  // toward the light, in the mirrored frame (x runs from the outer edge)
  for (int py = 0; py < H; ++py)
    for (int px = 0; px < W; ++px) {
      const double x = (right ? W - 1 - px : px) + 0.5, y = py + 0.5;
      const double dx = std::min(x - cxs, 0.0), dy = y - cys, r = std::max(1e-6, std::hypot(dx, dy));
      const double dirx = dx / r, diry = dy / r;
      double alpha = 1.0 - std::clamp(kWindow + 0.5 - r, 0.0, 1.0);  // opaque everywhere but the window (the corners outside the ring are background)
      const double outer = std::clamp(kOuter + 0.5 - r, 0.0, 1.0);
      double t;
      if (r < kRing) {
        t = 0.0;  // the gap between the ring and the bar
      } else {
        // across the ring's width it is a rounded rod: shade from the surface normal
        const double q = std::clamp((r - kRing) / (kOuter - kRing), 0.0, 1.0), nr = -std::cos(q * kPi), nz = std::sqrt(std::max(0.0, 1.0 - nr * nr));
        const double lit = nr * (dirx * lx + diry * ly) * 0.62 + nz * 0.78;  // light from the top left and in front
        t = 0.12 + 0.88 * std::clamp(lit, 0.0, 1.0) * 0.95;
        if (r > kOuter - 0.9) t -= 0.25;                                    // a dark edge against the frame
      }
      if (alpha <= 0) continue;
      t *= outer;  // fade the ring's edge into the background
      const size_t i = static_cast<size_t>(py) * W + px;
      img->tone[i] = static_cast<std::uint8_t>(std::clamp(t, 0.0, 1.0) * 255.0 + 0.5);
      img->alpha[i] = static_cast<std::uint8_t>(alpha * 255.0 + 0.5);
    }
  return cache[right] = img;
}


// ---- the right-hand column ----------------------------------------------------------------------------------------

constexpr const char* kActionName[6] = {"Attack", "Charm", "Scroll", "Hide", "Trick", "Pray"};

// The whole floor over the view, with a key to its pictograms at the right.
void drawFullMap(Ctx& c, int vx, int vy, int vpCols, int vpRows, const MapPicture& map, int floorNo) {
  c.clear(vx, vy, vpCols, vpRows);
  constexpr int kLegendCols = 28;
  const int size = std::clamp(std::min((vpCols - kLegendCols) * 2 / std::max(1, map.cols), vpRows * 4 / std::max(1, map.rows)), 4, 8);
  const int mw = mapCellsWide(map.cols, size), mh = mapCellsTall(map.rows, size);
  const int mx = vx + std::max(0, (vpCols - kLegendCols - mw) / 4), my = vy + std::max(0, (vpRows - mh) / 2);
  drawMapPicture(c.g, c.th, c.bg, mx, my, map, size);
  const int lx = mx + mw + 2;
  c.text(lx, vy + 1, "FLOOR " + std::to_string(floorNo), c.hi);
  struct Entry { MapTile tile; const char* name; };
  static const Entry kEntries[] = {{MapTile::Player, "You"},        {MapTile::DoorClosed, "Door"},   {MapTile::DoorOpen, "Open door"}, {MapTile::Fountain, "Fountain"},
                                   {MapTile::Elevator, "Elevator"}, {MapTile::Boss, "Boss"},         {MapTile::Chest, "Chest"}};
  int y = vy + 5;
  for (const Entry& e : kEntries) {
    MapPicture icon;
    icon.cols = icon.rows = 1;
    icon.tile = {e.tile};
    icon.facing = 0;
    drawMapPicture(c.g, c.th, c.bg, lx, y, icon, 8);  // 4 x 2 cells
    c.text(lx + 6, y, e.name, c.fg);
    y += 3;
  }
  c.text(lx, vy + vpRows - 3, "[M/Esc] Close", c.dim);
}

// The six actions as a grid, two per row. Arrow keys move the highlight; Enter or Z uses the highlighted action.
// Each button is 4 rows: border, the two rows the (11/6-cell-wide, two-row-tall) label occupies, border.
constexpr int kActionRows = 4;
void drawButton(Ctx& c, int bx, int by, int bw, const std::string& label, bool on, bool usable) {
  c.clear(bx, by, bw, kActionRows);
  if (on) {
    // Solid in the HP bar's color. The outline glyphs sit mid-cell, so the edge cells are half blocks: the fill
    // ends exactly where the line would run, instead of spilling half a cell outside it.
    const Rgb f = c.th.selectFill;
    c.g.fill(bx + 1, by + 1, bw - 2, kActionRows - 2, U' ', c.fg, f);
    for (int k = 1; k < bw - 1; ++k) {
      c.g.put(bx + k, by, U'\u2584', f, c.bg);
      c.g.put(bx + k, by + kActionRows - 1, U'\u2580', f, c.bg);
    }
    for (int j = 1; j < kActionRows - 1; ++j) {
      c.g.put(bx, by + j, U'\u2590', f, c.bg);
      c.g.put(bx + bw - 1, by + j, U'\u258C', f, c.bg);
    }
    c.g.put(bx, by, U'\u2597', f, c.bg);
    c.g.put(bx + bw - 1, by, U'\u2596', f, c.bg);
    c.g.put(bx, by + kActionRows - 1, U'\u259D', f, c.bg);
    c.g.put(bx + bw - 1, by + kActionRows - 1, U'\u2598', f, c.bg);
  } else {
    c.rect(bx, by, bw, kActionRows, usable ? c.line : mix(c.bg, c.line, 0.4));
  }
  const int inner = bw - 2, textCells12 = static_cast<int>(label.size()) * 11 * 2;  // label width in twelfths of a cell
  const int off = std::max(0, (inner * 12 - textCells12 + 12) / 24);              // centred, rounded to a whole cell
  c.g.printText(bx + 1 + off, by + 1, 0, 0, label, on ? c.th.onGood : usable ? c.fg : mix(c.bg, c.dim, 0.5));
}

void drawActionGrid(Ctx& c, int x, int y, int bw, const CombatHud& h) {
  for (int i = 0; i < 6; ++i)
    drawButton(c, x + (i % 2) * bw, y + (i / 2) * kActionRows, bw, kActionName[i], i == h.selected, h.enabled[static_cast<size_t>(i)]);
}

// A label and its bar: the numbers over the right of the label row, the bar on the row below.
void drawGauge(Ctx& c, int x, int y, int w, const std::string& label, int value, int maxValue, Rgb fill, bool low) {
  const double frac = maxValue > 0 ? static_cast<double>(value) / maxValue : 0.0;
  c.text(x, y, label, c.fg);
  c.textR(x, y, w, std::to_string(value) + "/" + std::to_string(maxValue), low ? c.th.danger : c.dim);
  c.bar(x, y + 2, w, frac, fill, mix(c.bg, fill, 0.2));
}

// The character: HP, MP, gold, experience. (The name and level are in the rule above.)
void drawHero(Ctx& c, int x, int y, int w, const HudInfo& h) {
  drawGauge(c, x, y, w, "HP", std::max(0, h.hp), h.maxHP, h.hpHurt ? c.th.danger : c.th.good, h.hp * 4 <= h.maxHP);
  drawGauge(c, x, y + 3, w, "MP", h.mp, h.maxMP, c.th.mana, false);
  c.text(x, y + 6, "Gold", c.fg);
  c.textR(x, y + 6, w, std::to_string(h.gold), c.th.coin);
  c.text(x, y + 8, "XP", c.fg);
  c.textR(x, y + 8, w, std::to_string(h.xp) + "/" + std::to_string(h.xpNeeded), c.dim);
  c.bar(x, y + 10, w, h.xpNeeded > 0 ? static_cast<double>(h.xp) / h.xpNeeded : 0.0, c.dim, mix(c.bg, c.dim, 0.25));
}

// A charmee slot (name, level, HP, bar), or a dim empty slot so the positions never move. Three rows tall.
void drawCharmee(Ctx& c, int x, int y, int w, const CharmeeInfo* m) {
  if (!m) {
    c.text(x, y, "(empty)", mix(c.bg, c.dim, 0.5));
    c.bar(x, y + 2, w, 0, c.dim, mix(c.bg, c.dim, 0.15));
    return;
  }
  const std::string title = m->name + " L" + std::to_string(m->level), hp = std::to_string(std::max(0, m->hp)) + "/" + std::to_string(m->maxHP);
  c.text(x, y, title.substr(0, static_cast<size_t>(c.chars(w))), c.fg);
  const double frac = m->maxHP > 0 ? static_cast<double>(std::max(0, m->hp)) / m->maxHP : 0.0;
  const Rgb fill = c.th.charmee, empty = mix(c.bg, fill, 0.2);
  c.bar(x, y + 2, w, frac, fill, empty);
  // The HP over the middle of the bar, one character at a time: letters over the filled part take whichever of black
  // and the text color contrasts more with the fill, letters over the empty part the normal text color.
  constexpr float kHpScale = 0.6f;                              // the glyph is about as tall as the one-row bar
  const double pitch = kTextSixths * kHpScale / 6.0;            // cells per character
  const Rgb black{0, 0, 0}, onFill = contrast(black, fill) >= contrast(c.fg, fill) ? black : c.fg;  // pure black: the filter's glow lightens anything less
  const int start = std::max(0, static_cast<int>((w / pitch - static_cast<double>(hp.size())) / 2.0));
  const float dy = static_cast<float>((1.0 - pitch) / 2.0);
  for (size_t i = 0; i < hp.size(); ++i) {
    const double mid = (static_cast<double>(start) + static_cast<double>(i) + 0.5) * pitch;
    const Rgb col = mid < frac * w ? onFill : c.fg;
    for (int pass = 0; pass < 2; ++pass)  // twice: the thin strokes of small text otherwise blend into the bar
      c.g.printText(x, y + 2, start + static_cast<int>(i), 0, std::string(1, hp[i]), col, kHpScale, dy);
  }
}

// The newest `lines` lines of the log, long messages wrapped; the newest message is bright.
void drawLog(Ctx& c, int x, int y, int w, int lines, const std::vector<std::string>& log) {
  struct Row { std::string text; bool first, newest, red; };  // red: the row starts inside a marked monster name
  std::vector<Row> rows;
  const int total = static_cast<int>(log.size()), width = std::max(1, c.chars(w - 4));
  for (int i = std::max(0, total - lines); i < total; ++i) {
    const auto parts = wrap(log[static_cast<size_t>(i)], width);
    bool red = false;
    for (size_t k = 0; k < parts.size(); ++k) {
      rows.push_back({parts[k], k == 0, i == total - 1, red});
      for (char ch : parts[k]) {
        if (ch == '\x01') red = true;
        else if (ch == '\x02') red = false;
      }
    }
  }
  const int first = std::max(0, static_cast<int>(rows.size()) - lines);
  for (int i = first; i < static_cast<int>(rows.size()); ++i) {
    const int r = (i - first) * 2;
    if (rows[static_cast<size_t>(i)].first) c.text(x, y + r, "> ", c.dim);
    const Row& row = rows[static_cast<size_t>(i)];
    const Rgb base = row.newest ? c.hi : c.fg;
    bool red = row.red;
    std::string seg;
    int n = 0;  // characters drawn so far on this row
    const auto flush = [&] {
      if (seg.empty()) return;
      c.text(x + 4 + n * 11 / 6, y + r, seg, red ? c.th.danger : base);
      n += static_cast<int>(seg.size());
      seg.clear();
    };
    for (char ch : row.text) {
      if (ch == '\x01' || ch == '\x02') { flush(); red = ch == '\x01'; }
      else seg += ch;
    }
    flush();
  }
}

constexpr int kMenuLeftCols = 52;  // the width of a Left sub-menu

void drawPage(Ctx& c, const MenuPanel& m, int x, int y, int w, int h) {
  c.clear(x, y, w, h);
  c.frame(x, y, w, h, m.title, c.hi);
  const int pad = 3;
  int tx = x + pad;  // the tabs
  for (size_t i = 0; i < m.tabs.size(); ++i) {
    const bool on = static_cast<int>(i) == m.tab;
    const std::string t = m.tabs[i];
    const int tw = static_cast<int>(t.size()) * 11 / 6 + 2;
    if (on) highlightRow(c, tx - 1, y + 1, tw + 1);
    c.text(tx, y + 1, t, on ? c.th.onGood : c.dim);
    tx += tw + 2;
  }
  const int ruleY = y + 3;
  for (int k = x + 1; k < x + w - 1; ++k) c.g.put(k, ruleY, c.ln.h, c.line, c.bg);
  c.g.put(x, ruleY, c.ln.lt, c.line, c.bg);
  c.g.put(x + w - 1, ruleY, c.ln.rt, c.line, c.bg);
  const int total = static_cast<int>(m.pageLines.size());
  const int first = std::clamp(m.scroll, 0, std::max(0, total - kPageLines));
  std::vector<OverlayLine> shown;
  for (int i = first; i < std::min(total, first + kPageLines); ++i) shown.push_back(m.pageLines[static_cast<size_t>(i)]);
  drawFacts(c, x + pad, ruleY + 1, w - 2 * pad, ruleY + 1 + 2 * kPageLines, shown);
  if (first > 0) c.g.put(x + w - 3, ruleY, U'▲', c.hi, c.bg);
  if (first + kPageLines < total) c.g.put(x + w - 3, y + h - 1, U'▼', c.hi, c.bg);
  if (!m.footer.empty()) c.text(x + pad, y + h - 3, m.footer, toneColor(c, m.footerTone));
}

void drawMenu(Ctx& c, const MenuPanel& m, int vx, int vy, int vpCols, int vpRows, int inset) {
  if (m.place == MenuPanel::Place::Page) {
    drawPage(c, m, vx, vy, vpCols, vpRows);
    return;
  }
  const bool wide = m.place == MenuPanel::Place::Wide;
  const int x = vx, y = vy + inset, availH = vpRows - inset;
  const int listW = wide ? (m.listW > 0 ? m.listW : 58) : kMenuLeftCols;
  const int w = wide ? vpCols : listW;
  const int pad = 2;  // cells between the frame and the text
  const int n = static_cast<int>(m.rows.size());
  // How tall: the list, then (Left) the details under it, or (Wide) the whole picture.
  const int detailRows = wide ? 0 : static_cast<int>(m.detail.size()) * 2;
  const int footerRows = m.footer.empty() ? 0 : 3;  // rule + text + gap... the rule shares the frame's last row
  const int chrome = 1 + 1 + (wide ? 0 : 1) + footerRows + 1;  // top frame, top gap, rule over the details, footer, bottom frame
  int h = wide ? availH : std::min(availH, chrome + 2 * n + detailRows);
  int listRows = std::max(1, (h - chrome - detailRows - (wide ? 2 * static_cast<int>(m.extra.size()) + 1 + (m.extraNote.empty() ? 0 : 2) : 0)) / 2);
  listRows = std::min(listRows, n);
  if (!wide) h = std::min(availH, chrome + 2 * listRows + detailRows);
  c.clear(x, y, w, h);
  c.frame(x, y, w, h, m.title, c.hi);
  const int first = n > listRows ? std::clamp(m.selected - listRows / 2, 0, n - listRows) : 0;
  const int rowW = listW - 2 * pad + 2;
  int ry = y + 2;
  for (int i = first; i < first + listRows; ++i, ry += 2) {
    const MenuRow& r = m.rows[static_cast<size_t>(i)];
    const bool on = i == m.selected;
    const int rx = x + pad - 1;
    if (on) highlightRow(c, rx, ry, rowW);
    const Rgb text = on ? c.th.onGood : r.enabled ? c.fg : mix(c.bg, c.dim, 0.5);
    c.text(rx + 1, ry, r.left.substr(0, static_cast<size_t>(c.chars(rowW - 2 - static_cast<int>(r.right.size()) * 11 / 6 - 1))), text);
    if (!r.right.empty()) c.textR(rx + 1, ry, rowW - 2, r.right, on ? c.th.onGood : r.enabled ? toneColor(c, r.tone) : mix(c.bg, c.dim, 0.5));
  }
  if (n > listRows) {  // there is more above / below
    if (first > 0) c.g.put(x + listW - 4, y, U'▲', c.hi, c.bg);  // more above / below, set into the frame
    if (first + listRows < n) c.g.put(x + listW - 4, ry, U'▼', c.hi, c.bg);
  }
  const int footY = y + h - 3;
  const auto hrule = [&](int yy, int x0, int x1) {  // joins the frame on both sides, or stops at the divider
    for (int k = x0 + 1; k < x1; ++k) c.g.put(k, yy, c.ln.h, c.line, c.bg);
  };
  if (wide) {
    // the list's column ends in a divider; facts under the list, details at the right
    const int dx = x + listW;
    for (int yy = y + 1; yy < y + h - 1; ++yy) c.g.put(dx, yy, c.ln.v, c.line, c.bg);
    c.g.put(dx, y, c.ln.tt, c.line, c.bg);
    c.g.put(dx, y + h - 1, c.ln.bt, c.line, c.bg);
    if (!m.extra.empty()) {
      const int ey = ry + 1;
      c.hrule(x, dx, ey, m.extraTitle);
      const int fw = listW - 2 * pad;  // from the equipment rows' text edge to the same distance from the divider
      if (m.extraRight.empty()) {
        drawFacts(c, x + pad, ey + 2, fw, y + h - 1, m.extra);
      } else {
        const int colW = (fw - 3) / 2;
        drawFacts(c, x + pad, ey + 2, colW, y + h - 1, m.extra);
        drawFacts(c, x + pad + fw - 1 - colW, ey + 2, colW, y + h - 1, m.extraRight);
      }
      if (!m.extraNote.empty()) {
        const int ny = ey + 2 + 2 * static_cast<int>(std::max(m.extra.size(), m.extraRight.size()));
        int n = 0;  // characters set so far
        for (const OverlayLine& piece : m.extraNote) {
          c.text(x + pad + n * 11 / 6, ny, piece.left, toneColor(c, piece.tone));
          n += static_cast<int>(piece.left.size());
        }
      }
    }
    drawFacts(c, dx + 1 + pad + 1, y + 2, w - listW - 2 - 2 * pad - 1, y + h - 1, m.detail);
    if (!m.footer.empty()) c.text(dx + 1 + pad + 1, y + h - 3, m.footer, toneColor(c, m.footerTone));
  } else {
    const int ruleY = ry;
    hrule(ruleY, x, x + w - 1);
    c.g.put(x, ruleY, c.ln.lt, c.line, c.bg);
    c.g.put(x + w - 1, ruleY, c.ln.rt, c.line, c.bg);
    drawFacts(c, x + pad + 1, ruleY + 1, w - 2 * pad - 2, footY, m.detail);
    if (!m.footer.empty()) c.text(x + pad + 1, footY, m.footer, toneColor(c, m.footerTone));
  }
}

// Framed panel(s) over the view: a column each, with a title, lines and a footer.
void drawOverlay(Ctx& c, const OverlayBox& box, int vx, int vy, int vpCols, int vpRows, int smallTop) {
  const auto tone = [&](int t) { return toneColor(c, t); };
  const int n = static_cast<int>(box.columns.size());
  const bool small = box.smallW > 0;
  const int boxW = small ? box.smallW : vpCols, boxH = small ? box.smallH : vpRows, gap = 1;
  const int boxX = small ? vx + vpCols - boxW - 1 : vx, boxY = small ? vy + smallTop : vy;
  const int colW = (boxW - gap * (n - 1)) / n;
  if (!small) c.clear(vx, vy, vpCols, vpRows);  // wipe the whole picture, gaps and margins included
  for (int ci = 0; ci < n; ++ci) {
    const OverlayColumn& col = box.columns[static_cast<size_t>(ci)];
    const int x0 = boxX + ci * (colW + gap);
    c.clear(x0, boxY, colW, boxH);
    c.rect(x0, boxY, colW, boxH, col.selected ? c.hi : c.line);
    if (small) {  // the little corner boxes are short on room: lines packed from the top
      const int chars = c.chars(colW - 2), rows = (boxH - 2) / 2;
      int r = 0;
      const auto emit = [&](const OverlayLine& l) {
        if (r >= rows) return;
        const Rgb color = tone(l.tone);
        c.text(x0 + 1, boxY + 1 + r * 2, l.left.substr(0, static_cast<size_t>(chars)), color);
        if (!l.right.empty() && static_cast<int>(l.right.size()) <= chars) c.textR(x0 + 1, boxY + 1 + r * 2, colW - 2, l.right, color);
        ++r;
      };
      if (!col.title.empty()) emit({col.title, "", col.selected ? 1 : 2});
      for (const OverlayLine& l : col.lines) emit(l);
      if (!col.footer.empty()) {
        r = rows - 1;
        emit({col.footer, "", col.selected ? 1 : 2});
      }
      if (!col.buttons.empty()) {  // side by side along the bottom
        const int nb = static_cast<int>(col.buttons.size()), bw = (colW - 2) / nb;
        for (int i = 0; i < nb; ++i) drawButton(c, x0 + 1 + i * bw, boxY + boxH - 5, bw, col.buttons[static_cast<size_t>(i)], i == col.buttonSel, true);
      }
      continue;
    }

    // The big panels have room to spare: a margin all round, a centred title over a rule, the item's name centred, dotted
    // leaders from each stat to its number, and the prompt centred under a rule of its own.
    const Rgb frameCol = col.selected ? c.hi : c.line;
    constexpr int kPadX = 3;  // cells between the frame and the text
    const int innerX = x0 + 1 + kPadX, innerW = colW - 2 - 2 * kPadX;
    const int chars = c.chars(innerW);
    const auto centred = [&](int y, const std::string& t, Rgb color) {
      const int w = static_cast<int>(t.size()) * 11 / 6;
      c.text(innerX + std::max(0, (innerW - w) / 2), y, t.substr(0, static_cast<size_t>(chars)), color);
    };
    const auto rule = [&](int y) {  // joins the frame on both sides
      for (int x = x0 + 1; x < x0 + colW - 1; ++x) c.g.put(x, y, c.ln.h, frameCol, c.bg);
      c.g.put(x0, y, c.ln.lt, frameCol, c.bg);
      c.g.put(x0 + colW - 1, y, c.ln.rt, frameCol, c.bg);
    };
    int y = boxY + 2;
    if (!col.title.empty()) {
      centred(y, col.title, col.selected ? c.hi : c.dim);
      y += 3;
      rule(y);
      y += 2;
    }
    const int bodyEnd = boxY + boxH - (!col.buttons.empty() ? 8 : col.footer.empty() ? 2 : 7);  // the first row the body may not use
    bool first = true;
    const bool pictured = col.orb != 0 || col.scroll >= 0;
    if (col.scroll >= 0) {  // the scroll's picture at the left (square), the lines centred in what is left of the body
      const int rowsAvail = bodyEnd - 1 - y;
      const int picCols = std::min(2 * rowsAvail, innerW * 9 / 20);
      const int picRows = picCols / 2;
      const int py = y + (rowsAvail - picRows) / 2;
      if (picRows >= 8) drawScroll(c.g, innerX, py, picCols, picRows, col.scroll, scrollColors(c.th), col.scrollT);
      const int tx = innerX + picCols + 2, tw = innerW - picCols - 2;
      const int textRows = static_cast<int>(col.lines.size()) * 2;
      int ty = y + std::max(0, (rowsAvail - textRows) / 2);
      for (const OverlayLine& l : col.lines) {
        const int w = static_cast<int>(l.left.size()) * 11 / 6;
        c.text(tx + std::max(0, (tw - w) / 2), ty, l.left.substr(0, static_cast<size_t>(c.chars(tw))), tone(l.tone));
        ty += l.tone == 1 ? 4 : 2;  // the name gets a blank line under it
      }
    } else if (col.orb != 0) {  // the animated orb fills the body; the lines go centred under it
      const int textRows = static_cast<int>(col.lines.size()) * 2;
      const int orbRows = bodyEnd - 1 - textRows - y;
      if (orbRows >= 8) drawOrb(c.g, innerX, y, innerW, orbRows, col.orb == 1 ? c.th.good : c.th.mana, c.bg, col.orbT);
      int ty = bodyEnd - 1 - textRows + 1;
      for (const OverlayLine& l : col.lines) {
        centred(ty, l.left, tone(l.tone));
        ty += 2;
      }
    }
    for (const OverlayLine& l : pictured ? std::vector<OverlayLine>{} : col.lines) {
      if (y + 2 > bodyEnd) break;
      const Rgb color = tone(l.tone);
      if (l.right.empty()) {
        if (first && col.lines.size() > 1 && l.tone == 1) {  // the item's name: centred, with a blank line under it
          centred(y, l.left, color);
          y += 4;
        } else {
          c.text(innerX, y, l.left.substr(0, static_cast<size_t>(chars)), color);
          y += 2;
        }
      } else {
        const int lw = static_cast<int>(l.left.size()) * 11 / 6, rw = static_cast<int>(l.right.size()) * 11 / 6;
        c.text(innerX, y, l.left.substr(0, static_cast<size_t>(chars)), color);
        c.textR(innerX, y, innerW, l.right, color);
        for (int x = innerX + lw + 1; x < innerX + innerW - rw - 1; ++x) c.g.put(x, y + 1, U'·', mix(c.bg, c.dim, 0.5), c.bg);
        y += 2;
      }
      first = false;
    }
    if (!col.buttons.empty()) {  // buttons in the combat actions' look, side by side under a rule
      rule(boxY + boxH - 7);
      const int nb = static_cast<int>(col.buttons.size()), bw = innerW / nb;
      const int bx0 = innerX + (innerW - bw * nb) / 2;
      for (int i = 0; i < nb; ++i) drawButton(c, bx0 + i * bw, boxY + boxH - 6, bw, col.buttons[static_cast<size_t>(i)], i == col.buttonSel, true);
    } else if (!col.footer.empty()) {
      rule(boxY + boxH - 7);  // the prompt is a button in the combat actions' look, lit when this column is the chosen one
      drawButton(c, innerX, boxY + boxH - 6, innerW, col.footer, col.selected, true);
    }
  }
}

}  // namespace

void drawMenuOver(CellGrid& g, const Theme& th, const MenuPanel& menu, int x, int y, int cols, int rows) {
  Ctx c{g, th, th.fg, th.muted, th.accent, th.bg, th.frame, th.selection};
  c.ln = kLineStyles[2];
  drawMenu(c, menu, x, y, cols, rows, 0);
}

void drawMainScreen(CellGrid& g, const Theme& th, const MainScreenOptions& opt) {
  const int W = g.width(), H = g.height();
  const int vpCols = kViewportCols, vpRows = kViewportRows, vx = kViewportX, vy = kViewportY;
  const int ruleY = kLogRuleY, rx = vx + vpCols + 1, rw = kRightCols - 1;  // the column's cells right of its rule
  const Rgb fg = th.fg, bg = th.bg, hi = th.accent;
  g.clear(bg);
  Ctx c{g, th, fg, th.muted, hi, bg, th.frame, th.selection};
  c.ln = kLineStyles[2];  // double lines

  // ---- outlines ---------------------------------------------------------------------------------
  c.frame(0, 0, W, H, "LABYRINTH", c.line);
  c.hrule(0, W - 1, ruleY, "LOG");
  c.vrule(vx + vpCols, 0, ruleY);
  if (opt.hud && !opt.hud->place.empty()) {
    const std::string place = "[ " + opt.hud->place + " ]";
    g.print(vx + vpCols - 2 - static_cast<int>(place.size()), 0, place, hi, bg);
  }
  if (!opt.hints.empty()) c.hint(W - 2, ruleY, opt.hints);

  // ---- the first-person view ------------------------------------------------------------------------
  if (opt.view) {
    const Rgb glow = mix(hi, th.danger, 0.55 + 0.35 * opt.bossGlowPulse);  // a Boss10's lair: the object layer in the danger colour, breathing
    const ObjectColors oc = objectColors(th);
    drawViewLayers(g, vx, vy, *opt.view, hi, bg, opt.bossGlow ? &glow : nullptr, &oc);
  }
  // Pictures and text on the picture are drawn above the cells (see renderGrid), so a menu or overlay over the view would show them through.
  const bool covered = opt.menu || (opt.overlay && !opt.overlay->columns.empty());
  if (opt.townSprites && !covered) {
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
      run.light = t.lit ? hi : mix(bg, hi, 0.68);  // the mosaic's tones for the chosen sign (objects) and the others (walls)
      run.dark = bg;
      run.bilinear = true;
      g.addImage(run);
    }
  }
  if (opt.labels && !covered) {  // text on the picture (the town): a line with its text placed in the cells it covers; the highlighted row is solid
    for (const SceneLabel& l : *opt.labels) {
      const int x = vx + l.cx, y = vy + l.cy, tw = static_cast<int>(std::ceil(static_cast<double>(l.text.size()) * 11 * l.scale / 6)), rw = static_cast<int>(l.right.size()) * 11 / 6;
      if (l.button) {
        drawButton(c, x, y, l.w, l.text, l.filled, true);
        continue;
      }
      c.clear(x, y, l.w, 2);
      if (l.filled) g.fill(x, y, l.w, 2, U' ', fg, hi);
      const int room = l.w - (rw ? rw + 1 : 0);
      const std::string text = tw > room ? l.text.substr(0, static_cast<size_t>(c.chars(room) / l.scale)) : l.text;
      const int shown = static_cast<int>(std::ceil(static_cast<double>(text.size()) * 11 * l.scale / 6));
      const int tx = l.align == SceneLabel::Align::Left ? x : l.align == SceneLabel::Align::Right ? x + l.w - shown : x + (l.w - shown) / 2;
      Rgb col = l.filled ? bg : toneColor(c, l.tone);
      if (l.fade > 0) col = mix(l.filled ? hi : bg, col, 1.0 - l.fade);
      if (l.scale > 1.0f) {  // big letters, centred on the two rows
        const float dy = 1.0f - static_cast<float>(kTextSixths) * l.scale / 12.0f;
        for (size_t i = 0; i < text.size(); ++i) g.printText(tx, y, static_cast<int>(i), 0, std::string(1, text[i]), col, l.scale, dy);
      } else {
        c.text(tx, y, text, col);
      }
      if (rw) c.text(x + l.w - rw, y, l.right, l.filled ? bg : toneColor(c, l.rightTone));
    }
  }
  g.setViewport(vx, vy, vx + vpCols, vy + vpRows);  // screen effects treat the picture differently from the UI
  // The enemy's name and health across the whole top of the picture (four cell rows), set into the frame like a gauge: an
  // ornate rounded cap at each end (drawn as a picture), a slot with a light rail above and a dark one below, and the bar
  // inside. The bar fills from the left in proportion to its HP, the name and HP centred over it in big letters.
  constexpr int kBarRows = 4;
  if (opt.torch) {  // below the enemy's bar when there is one (the bar is UI)
    CellGrid::Torch t;
    t.on = true;
    t.rect = {vx, vy + (opt.combat ? kBarRows : 0), vx + vpCols, vy + vpRows};
    const double m = tuning().torch.darkColor;
    t.dark = Rgb{static_cast<std::uint8_t>(bg.r * m), static_cast<std::uint8_t>(bg.g * m), static_cast<std::uint8_t>(bg.b * m)};
    t.shift = opt.torchShift;
    t.pulse = opt.torchPulse;
    t.breath = opt.torchBreath;
    g.setTorch(t);
  }
  if (opt.combat) {
    const CombatHud& e = *opt.combat;
    g.setViewport(vx, vy + kBarRows, vx + vpCols, vy + vpRows);  // the bar is UI, so it gets the UI's glow, not the picture's gentler one
    const double frac = e.maxHP > 0 ? std::clamp(static_cast<double>(e.hp) / e.maxHP, 0.0, 1.0) : 0.0;
    constexpr int kCap = 6;                           // cells taken by the cap at each end
    const int inner = vpCols - 2 * kCap;              // cells of bar between the caps
    static const char32_t kPartial[] = {U' ', U'\u258F', U'\u258E', U'\u258D', U'\u258C', U'\u258B', U'\u258A', U'\u2589'};
    const Rgb fill = th.gauge;                        // `danger`, adjusted so the label reads on it (Theme.cpp)
    const Rgb slot = mix(bg, fill, 0.14);             // the empty part of the bar
    const Rgb railTop = mix(th.frame, fg, 0.7), railBottom = mix(bg, th.frame, 0.6);  // lit from above: a much lighter rail over, a much darker one under
    // the bar runs the full width, a cell in from each edge (the round ends of its window), with the holders drawn over the ends
    const int barCells = vpCols - 2;
    const double fillCells = frac * barCells;
    for (int x = 0; x < vpCols; ++x) {
      const double left = fillCells - (x - 1);  // how much of this cell is filled
      for (int r = 0; r < kBarRows; ++r) {
        const bool rail = r == 0 || r == kBarRows - 1;
        if (rail) {
          const Rgb under = left >= 0.5 ? fill : slot;  // the bar showing in the half cell beside the rail
          g.put(vx + x, vy + r, r == 0 ? U'\u2580' : U'\u2584', r == 0 ? railTop : railBottom, under);
        } else if (left >= 1.0) g.put(vx + x, vy + r, U'\u2588', fill, bg);
        else if (left > 0.0) g.put(vx + x, vy + r, kPartial[std::clamp(static_cast<int>(left * 8), 0, 7)], fill, slot);
        else g.put(vx + x, vy + r, U' ', fill, slot);
      }
    }
    for (const bool right : {false, true}) {
      ImageRun cap;
      cap.img = barCapImage(right);
      cap.cx = right ? vx + vpCols - kCap : vx;
      cap.cy = vy;
      cap.srcScale = 3;
      cap.clipX0 = cap.cx;
      cap.clipX1 = cap.cx + kCap;
      cap.clipY0 = vy;
      cap.clipY1 = vy + kBarRows;
      cap.light = mix(th.frame, th.accent, 0.6);
      cap.dark = bg;
      cap.bilinear = true;
      g.addImage(cap);
    }
    // the label, bigger than the usual text, in the same colour as the rest of the UI's text; centred between the caps
    constexpr float kLabel = 1.5f;
    const std::string label = e.name + " L" + std::to_string(e.level) + "   " + std::to_string(std::max(0, e.hp)) + "/" + std::to_string(e.maxHP) + " HP";
    const double pitch = kTextSixths * kLabel / 6.0;  // cells per character
    const int chars = std::max(0, static_cast<int>(inner / pitch));
    const std::string shown = label.substr(0, static_cast<size_t>(chars));
    const int start = std::max(0, (chars - static_cast<int>(shown.size())) / 2);
    const float dy = static_cast<float>(kBarRows / 2.0 - pitch / 2.0);  // the glyph is `pitch` cell rows tall (cells are twice as tall as wide): centre it on the bar
    for (size_t i = 0; i < shown.size(); ++i) {
      g.printText(vx + kCap, vy, start + static_cast<int>(i), 0, std::string(1, shown[i]), mix(bg, fill, 0.2), kLabel, dy + 0.09f);  // a dark drop shadow keeps it legible over the orange
      g.printText(vx + kCap, vy, start + static_cast<int>(i), 0, std::string(1, shown[i]), fg, kLabel, dy);
    }
  }
  if (opt.combat && !opt.combat->banner.empty()) {  // a monster's special action: a flash band across the view with its name in big letters
    const CombatHud& e = *opt.combat;
    const Rgb tone = e.bannerKind == 3 ? th.coin : e.bannerKind == 4 ? th.mana : e.bannerKind == 5 ? th.accent : th.danger;
    const int row = vy + kBarRows + 1, rows = e.bannerHint.empty() ? 3 : 5;  // the death banner is taller: a line below for the prompt
    const double flash = e.bannerT < 0.3 ? 0.65 - e.bannerT : 0.35;  // bright at first, then settles
    g.addTorchHole(vx, row, vpCols, rows);  // the torch must not dim it
    g.fill(vx, row, vpCols, rows, U' ', fg, mix(bg, tone, std::clamp(flash, 0.2, 0.65)));
    constexpr float kBig = 2.0f;
    const double pitch = kTextSixths * kBig / 6.0;
    const int chars = static_cast<int>(e.banner.size());
    const int start = std::max(0, (static_cast<int>(vpCols / pitch) - chars) / 2);
    const int titleRows = 3;  // the big letters keep the 3-row band; the prompt sits on the rows beneath
    const float dy = static_cast<float>(titleRows / 2.0 - pitch / 2.0);
    for (int i = 0; i < chars; ++i) {
      g.printText(vx, row, start + i, 0, std::string(1, e.banner[static_cast<size_t>(i)]), mix(bg, tone, 0.35), kBig, dy + 0.09f);  // dark letters on the light band
      g.printText(vx, row, start + i, 0, std::string(1, e.banner[static_cast<size_t>(i)]), bg, kBig, dy);
    }
    if (!e.bannerHint.empty()) {
      const int tw = static_cast<int>(std::ceil(e.bannerHint.size() * kTextSixths / 6.0));
      const int hintCol = std::max(0, (vpCols - tw) / 2);
      const double breathe = 0.5 + 0.5 * std::sin(e.bannerT * 4.0);  // a slow pulse so the prompt draws the eye
      const int textRows = static_cast<int>(std::ceil(kTextSixths / 6.0));
      g.printText(vx + hintCol, row + titleRows + (rows - titleRows - textRows) / 2, 0, 0, e.bannerHint, mix(bg, fg, 0.55 + 0.45 * breathe));
    }
  }
  if (opt.combat && !opt.combat->status.empty()) {  // the player's ailments, on the view's bottom row
    const std::string t = " " + opt.combat->status + " ";
    const int textRows = static_cast<int>(std::ceil(kTextSixths / 6.0));  // the glyph is 11/6 cells wide, so about as many rows tall: it must not reach the frame rule
    const int tw = static_cast<int>(std::ceil(t.size() * kTextSixths / 6.0));
    g.addTorchHole(vx + 1, vy + vpRows - textRows - 1, tw, textRows);
    g.fill(vx + 1, vy + vpRows - textRows - 1, tw, textRows, U' ', fg, th.accent);  // a plate in the accent green so the text reads over any picture
    g.printText(vx + 1, vy + vpRows - textRows - 1, 0, 0, t, th.danger);
  }
  if (opt.combat && opt.combat->image && !opt.combat->image->empty()) {
    const auto img = opt.combat->image;
    float kScale = 0.88f;  // monsters stand in the room, not on the wall, and tower over the eye (the horizon is mid-view): a bit under the original size
    constexpr int kFloorGap = 5;     // the feet's row, sub-pixels above the bottom of the viewport (the shadow sits across it)
    const bool big = opt.combat->boss10;
    if (big) {  // a Boss10 is bigger, as far as the view allows
      const float want = kScale * static_cast<float>(tuning().monsters.boss10Scale);
      const float fitH = (vpRows * 4 - kBarRows * 4 - 6) * 2.0f / std::max(1, img->h), fitW = (vpCols * 2 - 4) * 2.0f / std::max(1, img->w);
      kScale = std::max(kScale, std::min({want, fitH, fitW}));
    }
    // Where the feet are: the lowest opaque row, and the horizontal span of the bottom few rows (sprites have different margins).
    int footRow = 0;
    for (int y = img->h - 1; y >= 0 && !footRow; --y)
      for (int x = 0; x < img->w; ++x)
        if (img->alpha[static_cast<size_t>(y) * img->w + x] > 128) { footRow = y + 1; break; }
    int fx0 = img->w, fx1 = 0;
    for (int y = std::max(0, footRow - 8); y < footRow; ++y)
      for (int x = 0; x < img->w; ++x)
        if (img->alpha[static_cast<size_t>(y) * img->w + x] > 128) { fx0 = std::min(fx0, x); fx1 = std::max(fx1, x + 1); }
    if (fx1 <= fx0) { fx0 = 0; fx1 = img->w; }
    const float toSub = kScale / 2.0f;  // image pixel -> sub-pixel (the offline art is 2x)
    const int menuShift = opt.menu && opt.menu->place == MenuPanel::Place::Left ? kMenuLeftCols + 1 : 0;  // a sub-menu at the left: stand in the room that is left
    const int sx0 = static_cast<int>(std::lround((vpCols * 2 + menuShift * 2 - img->w * toSub) / 2));  // centered
    int floorY = vpRows * 4 - kFloorGap;
    const double hover = opt.combat->hover;  // a flyer floats above its shadow
    const int lift = static_cast<int>(std::lround(hover * footRow * toSub));
    int sy0 = floorY - lift - static_cast<int>(std::lround(footRow * toSub));
    if (sy0 < kBarRows * 4) {  // tall sprite: keep it below the health bar (a flyer comes down first, then the feet drop)
      const int need = kBarRows * 4 - sy0;
      const int down = std::min(need, lift);
      sy0 += down;
      if (need > down) floorY += need - down;
      sy0 = std::max(sy0, kBarRows * 4);
    }
    // Contact shadow: a flat dithered ellipse under the feet, a little wider than they are.
    {
      const float footW = (hover > 0 ? img->w * 0.55f : (fx1 - fx0) * 1.3f) * toSub;  // a flyer's shadow is under its whole body, not under feet
      const int sw = std::max(8, static_cast<int>(std::lround(footW))), sh = 5;
      auto shadow = std::make_shared<Image8>();
      shadow->w = sw;
      shadow->h = sh;
      shadow->tone.assign(static_cast<size_t>(sw) * sh, 0);
      shadow->alpha.assign(static_cast<size_t>(sw) * sh, 0);
      for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
          const float u = (x + 0.5f) / sw * 2 - 1, v = (y + 0.5f) / sh * 2 - 1;
          const float d = u * u + v * v;
          if (d <= 1.0f && (d < 0.45f || (x + y) % 2 == 0)) shadow->alpha[static_cast<size_t>(y) * sw + x] = 255;  // solid core, dithered rim
        }
      ImageRun sr;
      sr.img = shadow;
      sr.cx = vx;
      sr.cy = vy;
      sr.sx = sx0 + static_cast<int>(std::lround((hover > 0 ? img->w : fx0 + fx1) * toSub / 2)) - sw / 2;
      sr.sy = floorY - sh / 2;
      sr.clipX0 = vx;
      sr.clipY0 = vy + kBarRows;
      sr.clipX1 = vx + vpCols;
      sr.clipY1 = vy + vpRows;
      sr.light = sr.dark = mix(bg, hi, 0.06);
      g.addImage(sr);
    }
    if (big) {  // the glow: two rings round the silhouette, red, breathing
      const Aura aura = auraFor(img);
      const double pulse = opt.combat->pulse;
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
        ar.light = ar.dark = mix(bg, th.danger, ring == 0 ? 0.22 + 0.16 * pulse : 0.42 + 0.30 * pulse);
        g.addImage(ar);
      }
    }
    ImageRun run;
    run.img = img;
    run.cx = vx;
    run.cy = vy;
    run.srcScale = 2;  // the offline asset is 2x the original resolution
    run.scale = kScale;
    run.sx = sx0;
    run.sy = sy0;
    run.clipX0 = vx;
    run.clipY0 = vy + kBarRows;
    run.clipX1 = vx + vpCols;
    run.clipY1 = vy + vpRows;
    run.light = big ? mix(hi, th.danger, 0.7) : hi;
    run.dark = big ? mix(bg, th.danger, 0.2) : mix(bg, hi, 0.08);
    run.bilinear = false;  // hard, aliased edges
    run.glow = true;       // the monster glows in the CRT filter, to pop against the dungeon
    g.addImage(run);
  }


  // ---- right column ------------------------------------------------------------------------------
  const bool fighting = opt.combat != nullptr;
  g.print(rx + 1, 0, fighting ? "[ ACTION ]" : opt.guide ? "[ TOWN ]" : "[ MAP ]", hi, bg);
  if (fighting && !opt.combat->over) {
    drawActionGrid(c, rx + (rw - 30) / 2, 1, 15, *opt.combat);
  } else if (fighting) {
    c.text(rx + 2, 4, "YOU DIED", th.danger);
    c.text(rx + 2, 6, "[Enter/Z] Continue", c.fg);
  } else if (opt.guide) {
    drawFacts(c, rx + 2, 2, rw - 4, 12, *opt.guide);
  } else if (opt.minimap) {
    drawMapPicture(g, th, bg, rx + (rw - mapCellsWide(kMinimapSize, kMinimapTile)) / 2, 2, *opt.minimap, kMinimapTile);
  }
  if (const HudInfo* h = opt.hud) {
    c.hrule(vx + vpCols, W - 1, 13, h->name + "  Lv " + std::to_string(h->level));
    drawHero(c, rx + 1, 14, rw - 2, *h);
    c.hrule(vx + vpCols, W - 1, 25, "CHARMEES");
    for (int i = 0; i < 2; ++i)
      drawCharmee(c, rx + 1, 26 + i * 4, rw - 2, static_cast<size_t>(i) < h->charmees.size() ? &h->charmees[static_cast<size_t>(i)] : nullptr);
  }

  // ---- log -----------------------------------------------------------------------------------------------
  if (opt.log) drawLog(c, 2, ruleY + 1, W - 4, kLogLines, *opt.log);

  if (opt.overlay && !opt.overlay->columns.empty()) drawOverlay(c, *opt.overlay, vx, vy, vpCols, vpRows, opt.combat ? kBarRows + 1 : 1);
  if (opt.fullMap) drawFullMap(c, vx, vy, vpCols, vpRows, *opt.fullMap, opt.floorNumber);
  if (opt.menu) drawMenu(c, *opt.menu, vx, vy, vpCols, vpRows, opt.combat ? kBarRows : 0);
  if (!opt.bottomHints.empty()) g.print(2, H - 1, opt.bottomHints, c.dim, bg);
  if (!opt.statusTag.empty()) {
    const std::string tag = "[ " + opt.statusTag + " ]";
    g.print(std::max(1, W - 2 - static_cast<int>(tag.size())), H - 1, tag, c.dim, bg);
  }
}

}  // namespace laby
