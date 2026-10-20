#include <tuple>
#include "ui/TitleScreen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

#include "render/Mosaic.hpp"
#include "ui/PanelKit.hpp"
#include "ui/ViewPainter.hpp"

namespace laby {
namespace {

std::uint32_t hash32(std::uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
double unit(std::uint32_t seed) { return (hash32(seed) & 0xFFFFFF) / 16777216.0; }

void dot(Bitmap1& b, int x, int y) {
  if (x >= 0 && y >= 0 && x < b.w && y < b.h) b.px[static_cast<size_t>(y) * static_cast<size_t>(b.w) + static_cast<size_t>(x)] = Bitmap1::Light;
}
void line(Bitmap1& b, int x0, int y0, int x1, int y1) {
  const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  for (;;) {
    dot(b, x0, y0);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx) { err += dx; y0 += sy; }
  }
}
// Lightning: every four seconds there is a chance of a flash, which flickers (on, off, on, off) over about a quarter of a second.
bool flashing(std::uint64_t ms) {
  const std::uint32_t window = static_cast<std::uint32_t>(ms / 4000);
  if (hash32(window * 2654435761U + 11) % 100 >= 55) return false;
  const std::uint64_t start = window * 4000ULL + hash32(window + 77) % 2600;
  if (ms < start) return false;
  const std::uint64_t t = ms - start;
  return t < 70 || (t >= 130 && t < 330) || (t >= 390 && t < 450);
}

// The author's original start-screen art (images/Laby-Title.png, TitleCloud.png, TitleCloud2.png from the Playdate game), bundled in
// <data>/art/title. Only the white line work is kept: the black background is not drawn.
const Bitmap1& originalArt(const char* name) {  // (a C string, so the compiler can see the result does not point at a temporary)
  static std::map<std::string, Bitmap1> cache;
  auto it = cache.find(name);
  if (it != cache.end()) return it->second;
  Bitmap1 b = loadOriginalArt(name);
  for (auto& p : b.px)
    if (p == Bitmap1::Dark) p = Bitmap1::Clear;
  return cache.emplace(name, std::move(b)).first->second;
}

void wipe(Bitmap1& b, int x0, int y0, int x1, int y1) {
  for (int y = std::max(0, y0); y < std::min(b.h, y1); ++y)
    for (int x = std::max(0, x0); x < std::min(b.w, x1); ++x) b.px[static_cast<size_t>(y) * static_cast<size_t>(b.w) + static_cast<size_t>(x)] = Bitmap1::Clear;
}

// Line art resampled to w x h: an output pixel is lit if any lit source pixel falls in it, so one-pixel lines survive shrinking.
Bitmap1 scaleLines(const Bitmap1& src, int w, int h) {
  Bitmap1 out(w, h);
  for (int y = 0; y < src.h; ++y)
    for (int x = 0; x < src.w; ++x)
      if (src.at(x, y) == Bitmap1::Light) dot(out, x * w / src.w, y * h / src.h);
  return out;
}

// The title picture (castle, moon, "Welcome To", the black-letter title) fitted to the sky, with the original's "Press A" and "How to Play"
// prompts and its credit taken out (the menu and real text do that now). `horizon` is the long line through the moon, kept apart so it
// can be drawn in the clouds' colour. Built once per size.
struct TitleArt {
  Bitmap1 art, horizon;
};
const TitleArt& titleArt(int W, int H) {
  static std::map<std::pair<int, int>, TitleArt> cache;
  auto it = cache.find({W, H});
  if (it != cache.end()) return it->second;
  Bitmap1 src = originalArt("Laby-Title");
  TitleArt out{Bitmap1(W, H), Bitmap1(W, H)};
  if (!src.empty()) {
    // the picture's own frame: a long straight line within a few pixels of an edge
    const auto lit = [&](int x, int y) { return src.at(x, y) == Bitmap1::Light; };
    for (int i = 0; i < 10; ++i) {
      for (const int x : {i, src.w - 1 - i}) {
        int n = 0;
        for (int y = 0; y < src.h; ++y) n += lit(x, y);
        if (n > src.h / 2) wipe(src, x, 0, x + 1, src.h);
      }
      for (const int y : {i, src.h - 1 - i}) {
        int n = 0;
        for (int x = 0; x < src.w; ++x) n += lit(x, y);
        if (n > src.w / 2) wipe(src, 0, y, src.w, y + 1);
      }
    }
    wipe(src, 160, 143, 375, 175);  // "Press (A) for Labyrinth." (the moon ends at row 140)
    wipe(src, 210, 175, 260, 180);  // the bottom of the A circle
    wipe(src, 270, 198, 400, 240);  // "(B) How to Play"
    wipe(src, 0, 205, 175, 240);    // "A game by Sam Charchian": too small to read once shrunk, drawn as text instead
    // the horizon line across the moon (rows 114-119, left and right of the moon's own outline)
    Bitmap1 line(src.w, src.h);
    for (int y = 114; y <= 119; ++y)
      for (int x = 195; x < 325; ++x) {
        const bool moonSide = (x >= 229 && x <= 241) || (x >= 277 && x <= 289);
        if (!moonSide && lit(x, y)) {
          line.px[static_cast<size_t>(y) * static_cast<size_t>(line.w) + static_cast<size_t>(x)] = Bitmap1::Light;
          src.px[static_cast<size_t>(y) * static_cast<size_t>(src.w) + static_cast<size_t>(x)] = Bitmap1::Clear;
        }
      }
    const double k = std::min(static_cast<double>(W) / src.w, static_cast<double>(H) / src.h);
    const int sw = static_cast<int>(src.w * k), sh = static_cast<int>(src.h * k);
    out.art.blit(scaleLines(src, sw, sh), (W - sw) / 2, 4);
    out.horizon.blit(scaleLines(line, sw, sh), (W - sw) / 2, 4);
  }
  return cache.emplace(std::make_pair(W, H), std::move(out)).first->second;
}

}  // namespace

bool drawStorm(CellGrid& g, const Theme& th, std::uint64_t ms, bool withArt) {
  const int W = g.width() * 2, H = g.height() * 4;
  const bool flash = flashing(ms);
  const Rgb bg = th.bg, hi = th.accent;
  const Rgb paper = flash ? mix(bg, hi, 0.45) : bg;
  g.clear(paper);

  Bitmap1 far(W, H), near(W, H), rain(W, H);
  // two decks of the original's cloud sprites drifting left at different speeds and sizes (the far one smaller and slower: parallax)
  const Bitmap1& cloudA = originalArt("TitleCloud");
  const Bitmap1& cloudB = originalArt("TitleCloud2");
  const auto deck = [&](Bitmap1& b, int count, int baseY, int span, double scaleX, double scaleY, double speed, std::uint32_t seed) {
    for (int i = 0; i < count; ++i) {
      const Bitmap1& spr = (hash32(seed + static_cast<std::uint32_t>(i) * 13) & 1) ? cloudA : cloudB;
      if (spr.empty()) continue;
      const int w = static_cast<int>(spr.w * scaleX), h = static_cast<int>(spr.h * scaleY);
      const double x0 = unit(seed + static_cast<std::uint32_t>(i) * 31) * span;
      const double x = std::fmod(x0 - ms * speed + span * 4.0, span) - w;
      const int y = baseY + static_cast<int>(unit(seed + static_cast<std::uint32_t>(i) * 17) * 40);
      static std::map<std::tuple<const Bitmap1*, int, int>, Bitmap1> scaled;  // the same few sizes every frame
      auto it = scaled.find({&spr, w, h});
      if (it == scaled.end()) it = scaled.emplace(std::make_tuple(&spr, w, h), scaleLines(spr, w, h)).first;
      b.blit(it->second, static_cast<int>(x), y);
    }
  };
  deck(far, 5, 6, W + 120, 0.55, 0.35, 0.0018, 101);
  deck(near, 4, 20, W + 160, 0.95, 0.6, 0.0036, 202);
  const Bitmap1 noArt(W, H);
  const Bitmap1& art = withArt ? titleArt(W, H).art : noArt;
  if (withArt) near.blit(titleArt(W, H).horizon, 0, 0);  // the line through the moon is cloud-coloured

  // rain: slanted streaks, each on its own cycle (a pure function of the clock)
  constexpr int kDrops = 25;
  for (int i = 0; i < kDrops; ++i) {
    const std::uint32_t s = static_cast<std::uint32_t>(i) * 977U + 5;
    const double speed = 0.45 + unit(s + 1) * 0.35;      // sub-pixels per ms
    const int len = 9 + static_cast<int>(unit(s + 2) * 28);  // 9 to 36 sub-pixels: the shortest streak to four times that
    const double cycle = H + len;
    const double y = std::fmod(unit(s + 3) * cycle + ms * speed, cycle) - len;
    const double x = std::fmod(unit(s + 4) * (W + 60) - y * 0.25 + (W + 60) * 4.0, W + 60.0) - 20;
    line(rain, static_cast<int>(x), static_cast<int>(y), static_cast<int>(x - len / 4.0), static_cast<int>(y + len));
  }

  const Rgb cloudFar = flash ? mix(paper, bg, 0.35) : mix(bg, hi, 0.3), cloudNear = flash ? mix(paper, bg, 0.6) : mix(bg, hi, 0.5);
  const Rgb artInk = flash ? bg : mix(bg, hi, 0.92);
  const Rgb rainInk = flash ? bg : mix(bg, hi, 0.6);
  drawMosaicStack(g, 0, 0, {{&art, artInk}, {&rain, rainInk}, {&near, cloudNear}, {&far, cloudFar}}, paper, /*invert=*/true);
  return flash;
}

namespace {

Ctx makeCtx(CellGrid& g, const Theme& th) {
  Ctx c{g, th, th.fg, th.muted, th.accent, th.bg, th.frame, th.selection};
  c.ln = kLineStyles[2];
  return c;
}

// The big title letters, centred on the grid at row `y`, with a drop shadow; `ink` on a flash is the dark background colour.
void bigTitle(CellGrid& g, const std::string& text, int y, float scale, Rgb ink, Rgb shadow) {
  const double pitch = 11 * scale / 6.0;  // cells per character
  const double total = pitch * static_cast<double>(text.size());
  const int x0 = static_cast<int>((g.width() - total) / 2);
  for (size_t i = 0; i < text.size(); ++i) {
    g.printText(x0, y, static_cast<int>(i), 0, std::string(1, text[i]), shadow, scale, 0.45f);
    g.printText(x0, y, static_cast<int>(i), 0, std::string(1, text[i]), ink, scale, 0.0f);
  }
}

}  // namespace

void drawTitleScreen(CellGrid& g, const Theme& th, std::uint64_t ms, const std::vector<std::string>& rows, int selected, int bigRow) {
  const bool flash = drawStorm(g, th, ms);
  Ctx c = makeCtx(g, th);
  const bool art = !originalArt("Laby-Title").empty();
  const Rgb dimText = flash ? th.bg : th.muted, textBg = flash ? mix(th.bg, th.accent, 0.45) : th.bg;
  if (!art) {  // the original's picture is not there: letters instead
    const Rgb ink = flash ? th.bg : th.accent, shadow = flash ? mix(th.bg, th.accent, 0.5) : mix(th.bg, th.accent, 0.22);
    bigTitle(g, "LABYRINTH", 8, 3.0f, ink, shadow);
  }

  // the credit, in real letters (the original's was a tiny bitmap font that blurred when the picture was scaled)
  c.text(4, g.height() - 5, "A game by Sam Charchian", flash ? th.bg : c.hi);

  // The choices, with no window round them: far right and low, so the picture shows. The first one ("Enter the Labyrinth") is half again
  // as big as the rest. (Without the art they sit in the middle.)
  constexpr float kBig = 1.5f;
  constexpr int kW = 58, kBigRows = 4, kPitch = 3;
  const int x = art ? g.width() - kW - 3 : (g.width() - kW) / 2, y0 = (art ? 27 : 20) - bigRow * kPitch;
  for (size_t i = 0; i < rows.size(); ++i) {
    const int ri = static_cast<int>(i);
    const bool on = ri == selected, big = ri == bigRow;
    const int y = ri < bigRow ? y0 + ri * kPitch : big ? y0 + bigRow * kPitch : y0 + bigRow * kPitch + kBigRows + 1 + (ri - bigRow - 1) * kPitch;
    const float scale = big ? kBig : 1.0f;
    const int rowsH = big ? kBigRows : 2;
    const double pitch = 11 * scale / 6.0;  // cells per character
    const int w = static_cast<int>(pitch * static_cast<double>(rows[i].size()));
    const int barW = std::min(kW, w + 6), barX = x + (kW - barW) / 2;  // the bar is as wide as its text, plus a margin
    if (on) {  // the action buttons' look: solid, ending in half blocks
      const Rgb f = th.selectFill;
      g.fill(barX + 1, y, barW - 2, rowsH, U' ', c.fg, f);
      for (int j = 0; j < rowsH; ++j) {
        g.put(barX, y + j, U'\u2590', f, c.bg);
        g.put(barX + barW - 1, y + j, U'\u258C', f, c.bg);
      }
    }
    const float dy = big ? static_cast<float>((kBigRows - pitch) / 2.0) : 0.0f;
    g.printText(x + (kW - w) / 2, y, 0, 0, rows[i], on ? th.onGood : c.fg, scale, dy);
  }
  const std::string hint = "[Arrows] Choose  [Enter/Z] Select  [Esc] Menu";
  g.print(4, g.height() - 2, hint, dimText, textBg);  // bottom left: the list of choices runs down the right side
  const std::string version = "v" LABYTERM_VERSION;
  g.print(g.width() - 4 - static_cast<int>(version.size()), g.height() - 2, version, dimText, textBg);  // bottom right, same quiet style
}

void drawCreationScreen(CellGrid& g, const Theme& th, std::uint64_t ms, const CreationInfo& info) {
  drawStorm(g, th, ms, false);
  Ctx c = makeCtx(g, th);
  const int W = 80, H = 40, x = (g.width() - W) / 2, y = 2;
  c.clear(x, y, W, H);
  c.frame(x, y, W, H, "NEW CHARACTER", c.hi);
  const int pad = 3, inner = W - 2 * pad - 2, left = x + 1 + pad;

  const auto button = [&](int row, int by, const std::string& label, const std::string& value, bool centred = false) {
    const bool on = info.selected == row;
    if (on) highlightRow(c, left - 1, by, inner + 2);
    c.text(centred ? left + (inner - static_cast<int>(label.size()) * 11 / 6) / 2 : left, by, label, on ? th.onGood : c.fg);
    if (!value.empty()) c.textR(left, by, inner, value, on ? th.onGood : c.dim);
  };
  {  // Name: the label, and a boxed field close after it (the box lights up when chosen, and more while you type)
    const bool on = info.selected == 0;
    const int by = y + 3, labelW = 11, fieldX = left + labelW, fieldW = 36;
    if (on) highlightRow(c, left - 1, by, labelW - 1);
    c.text(left, by, "Name", on ? th.onGood : c.fg);
    c.clear(fieldX, by - 1, fieldW, 4);
    // the box hugs the text: thin lines at the inner edge of the rows above and below (one-eighth blocks), not a frame a row away
    const Rgb edge = info.editing ? c.hi : on ? mix(c.line, c.hi, 0.6) : c.line;
    for (int k = 0; k < fieldW; ++k) {
      g.put(fieldX + k, by - 1, U'\u2581', edge, c.bg);
      g.put(fieldX + k, by + 2, U'\u2594', edge, c.bg);
    }
    for (int j = 0; j < 2; ++j) {
      g.put(fieldX, by + j, U'\u258F', edge, c.bg);
      g.put(fieldX + fieldW - 1, by + j, U'\u2595', edge, c.bg);
    }
    c.text(fieldX + 2, by, info.name + (info.editing ? "_" : ""), info.editing || on ? c.hi : c.fg);
  }

  const int colW = (inner - 4) / 2, rightX = left + colW + 4;
  c.text(left, y + 7, "Your stats", c.dim);
  c.text(rightX, y + 7, info.itemTitle, c.dim);
  for (int k = left; k < left + colW; ++k) g.put(k, y + 9, c.ln.h, c.line, c.bg);
  for (int k = rightX; k < rightX + colW; ++k) g.put(k, y + 9, c.ln.h, c.line, c.bg);
  drawFacts(c, left, y + 10, colW, y + 28, info.statLines);
  drawFacts(c, rightX, y + 10, colW, y + 28, info.itemLines);

  button(1, y + 30, "Re-Roll Character", "");
  {  // "Enter the Labyrinth!": half again as big as the other rows, on a bar to match (four cell rows instead of two)
    constexpr float kBig = 1.5f;
    constexpr int kRows = 4;
    const int by = y + 33;
    const bool on = info.selected == 2;
    if (on) {
      const Rgb f = th.selectFill;
      g.fill(left, by, inner, kRows, U' ', c.fg, f);
      for (int j = 0; j < kRows; ++j) {
        g.put(left - 1, by + j, U'\u2590', f, c.bg);
        g.put(left + inner, by + j, U'\u258C', f, c.bg);
      }
    }
    const std::string label = "Enter the Labyrinth!";
    const double pitch = 11 * kBig / 6.0;
    const int w = static_cast<int>(pitch * static_cast<double>(label.size()));
    g.printText(left + (inner - w) / 2, by, 0, 0, label, on ? th.onGood : c.fg, kBig, static_cast<float>((kRows - pitch) / 2.0));
  }
  const std::string hint = info.editing ? "Type a name  [Enter] Done  [Bksp] Delete" : "[Arrows] Choose  [Enter/Z] Select  [Esc] Menu";
  g.print((g.width() - static_cast<int>(hint.size())) / 2, g.height() - 2, hint, c.dim, c.bg);
}

void drawLegendsScreen(CellGrid& g, const Theme& th, std::uint64_t ms, const LegendsInfo& info) {
  const bool flash = drawStorm(g, th, ms, false);
  Ctx c = makeCtx(g, th);
  bigTitle(g, "LEGENDS", 4, 3.0f, flash ? th.bg : th.accent, flash ? mix(th.bg, th.accent, 0.5) : mix(th.bg, th.accent, 0.22));
  // Overlay text (11/6 cells per character, two rows high) instead of one-cell print text: about 83% bigger, with a panel to match.
  const int W = 100, H = 27, x = (g.width() - W) / 2, y = 12;
  c.clear(x, y, W, H);
  c.frame(x, y, W, H, "TOP 10", c.hi);
  const int left = x + 6, right = x + W - 7, nameX = left + 13, floorW = 10, scoreW = 18;
  const int floorX = right - scoreW - 3 - floorW;
  const auto cols = [&](int ry, const std::string& rank, const std::string& name, const std::string& fl, const std::string& sc, Rgb ink) {
    c.text(left, ry, rank, ink);
    c.text(nameX, ry, name, ink);
    c.textR(floorX, ry, floorW, fl, ink);
    c.textR(right - scoreW, ry, scoreW, sc, ink);
  };
  cols(y + 2, "RANK", "NAME", "FLOOR", "SCORE", c.dim);
  for (int k = left; k < right; ++k) g.put(k, y + 4, c.ln.h, c.line, c.bg);
  static const char* kOrdinal[] = {"1ST", "2ND", "3RD", "4TH", "5TH", "6TH", "7TH", "8TH", "9TH", "10TH"};
  const bool blinkOn = (ms / 450) % 2 == 0;
  for (int i = 0; i < 10; ++i) {
    const int ry = y + 5 + i * 2;
    const bool have = i < static_cast<int>(info.rows.size()), mine = info.highlight == i + 1;
    const Rgb ink = mine ? (blinkOn ? c.hi : c.dim) : have ? (i == 0 ? th.accent : c.fg) : c.dim;
    if (!have) {
      cols(ry, kOrdinal[i], "............", "", "", ink);
      continue;
    }
    const auto& r = info.rows[static_cast<size_t>(i)];
    cols(ry, kOrdinal[i], r.name.substr(0, 14), std::to_string(r.floor), std::to_string(r.score), ink);
  }
  const std::string hint = info.fromGameOver ? "[Enter/Z] Continue  [Esc] Title" : "[Enter/Z] Back  [Esc] Menu";
  g.print((g.width() - static_cast<int>(hint.size())) / 2, g.height() - 2, hint, c.dim, c.bg);
}

void drawGameOverScreen(CellGrid& g, const Theme& th, std::uint64_t ms, const GameOverInfo& info) {
  const bool flash = drawStorm(g, th, ms, false);
  Ctx c = makeCtx(g, th);
  bigTitle(g, "GAME OVER", 4, 3.0f, flash ? th.bg : th.danger, flash ? mix(th.bg, th.danger, 0.5) : mix(th.bg, th.danger, 0.22));
  const int W = 64, H = 29, x = (g.width() - W) / 2, y = 11;
  c.clear(x, y, W, H);
  c.frame(x, y, W, H, info.name.empty() ? "YOU DIED" : info.name, c.hi);
  const int pad = 3, inner = W - 2 * pad - 2, left = x + 1 + pad;
  drawFacts(c, left, y + 3, inner, y + 14, info.facts);
  for (int k = left; k < left + inner; ++k) g.put(k, y + 15, c.ln.h, c.line, c.bg);
  drawFacts(c, left, y + 17, inner, y + 25, info.score);
  highlightRow(c, left - 1, y + H - 3, inner + 2);
  c.text(left + (inner - 8 * 11 / 6) / 2, y + H - 3, "Continue", th.onGood);
  const std::string hint = "[Enter/Z] Continue  [Esc] Title";
  g.print((g.width() - static_cast<int>(hint.size())) / 2, g.height() - 2, hint, c.dim, c.bg);
}

}  // namespace laby
