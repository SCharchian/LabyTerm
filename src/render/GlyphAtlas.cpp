#include "render/GlyphAtlas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>

#include "stb_truetype.h"

namespace laby {

// ---- font loading ------------------------------------------------------------------------------

struct FontFace::Impl {
  std::vector<unsigned char> data;
  stbtt_fontinfo info{};
};

FontFace::FontFace() = default;
FontFace::~FontFace() = default;
FontFace::FontFace(FontFace&&) noexcept = default;
FontFace& FontFace::operator=(FontFace&&) noexcept = default;

bool FontFace::loaded() const { return impl_ != nullptr; }

bool FontFace::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  auto impl = std::make_unique<Impl>();
  impl->data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  if (impl->data.empty()) return false;
  const int offset = stbtt_GetFontOffsetForIndex(impl->data.data(), 0);
  if (offset < 0 || !stbtt_InitFont(&impl->info, impl->data.data(), offset)) return false;
  impl_ = std::move(impl);
  path_ = path;
  name_ = path.substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
  return true;
}

std::vector<FontCandidate> findFonts(const std::string& bundledDir) {
  std::vector<FontCandidate> out;
  std::set<std::string> seen;
  struct Bundled { const char* file; const char* label; const char* systemTwin; };
  for (const Bundled& b : {Bundled{"VT323-Regular.ttf", "VT323 (bundled)", ""},
                           Bundled{"AdwaitaMono-Regular.ttf", "Adwaita Mono (bundled)", "/usr/share/fonts/Adwaita/AdwaitaMono-Regular.ttf"}}) {
    const std::string path = bundledDir + "/" + b.file;
    std::ifstream probe(path, std::ios::binary);
    if (!probe) continue;
    out.push_back({b.label, path});
    if (*b.systemTwin) seen.insert(b.systemTwin);  // the same font installed system-wide
  }
  // monospace (the user's terminal font on Omarchy) first, then a few known good terminal fonts
  for (const char* family : {"monospace", "JetBrainsMono Nerd Font", "Adwaita Mono", "Liberation Mono", "iA Writer Mono S",
                             "Nimbus Mono PS"}) {
    const std::string cmd = std::string("fc-match -f '%{family}|%{file}' '") + family + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) continue;
    char buf[1024] = {};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, p);
    pclose(p);
    std::string s(buf, n);
    const auto bar = s.find('|');
    if (bar == std::string::npos) continue;
    std::string label = s.substr(0, bar), path = s.substr(bar + 1);
    if (const auto comma = label.find(','); comma != std::string::npos) label = label.substr(0, comma);
    if (path.empty() || !seen.insert(path).second) continue;
    if (path.size() < 4 || (path.substr(path.size() - 4) != ".ttf" && path.substr(path.size() - 4) != ".TTF")) continue;  // stb: TrueType only
    out.push_back({label, path});
  }
  return out;
}

// ---- mosaic code points ---------------------------------------------------------------------------

char32_t sextantGlyph(unsigned p) {
  p &= 63;
  if (p == 0) return U' ';
  if (p == 63) return U'█';
  if (p == 21) return U'▌';  // left half
  if (p == 42) return U'▐';  // right half
  // U+1FB00.. enumerate patterns 1..62 in order, skipping 21 and 42
  unsigned idx = p - 1;
  if (p > 21) --idx;
  if (p > 42) --idx;
  return static_cast<char32_t>(0x1FB00 + idx);
}

char32_t quadrantGlyph(unsigned p) {
  static constexpr char32_t kTable[16] = {U' ', U'▘', U'▝', U'▀', U'▖', U'▌', U'▞', U'▛',
                                          U'▗', U'▚', U'▐', U'▜', U'▄', U'▙', U'▟', U'█'};
  return kTable[p & 15];
}

// ---- drawing ----------------------------------------------------------------------------------------

namespace {

struct Canvas {
  int w, h;
  std::vector<std::uint8_t> a;
  Canvas(int w_, int h_) : w(w_), h(h_), a(static_cast<size_t>(w_) * static_cast<size_t>(h_), 0) {}
  void clear() { std::fill(a.begin(), a.end(), 0); }
  void put(int x, int y, std::uint8_t v) {
    if (x >= 0 && y >= 0 && x < w && y < h) a[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] = std::max<std::uint8_t>(v, a[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)]);
  }
  void rect(int x0, int y0, int x1, int y1, std::uint8_t v = 255) {  // [x0,x1) x [y0,y1)
    for (int y = std::max(0, y0); y < std::min(h, y1); ++y)
      for (int x = std::max(0, x0); x < std::min(w, x1); ++x) put(x, y, v);
  }
};

int rnd(double v) { return static_cast<int>(std::lround(v)); }

// Column/row edges that split a cell into n equal-ish parts (exact integers, so neighbours tile seamlessly)
std::vector<int> edges(int total, int n) {
  std::vector<int> e(static_cast<size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) e[static_cast<size_t>(i)] = rnd(static_cast<double>(total) * i / n);
  return e;
}

// Fill a cols x rows pixel grid pattern (bit index = row * cols + col)
void mosaic(Canvas& c, int cols, int rows, unsigned pattern) {
  const auto xe = edges(c.w, cols), ye = edges(c.h, rows);
  for (int r = 0; r < rows; ++r)
    for (int col = 0; col < cols; ++col)
      if (pattern & (1u << (r * cols + col)))
        c.rect(xe[static_cast<size_t>(col)], ye[static_cast<size_t>(r)], xe[static_cast<size_t>(col) + 1], ye[static_cast<size_t>(r) + 1]);
}

void shade(Canvas& c, int level) {  // 1 = 25%, 2 = 50%, 3 = 75%
  for (int y = 0; y < c.h; ++y)
    for (int x = 0; x < c.w; ++x) {
      bool on;
      if (level == 2) on = ((x + y) & 1) == 0;
      else if (level == 1) on = (x & 1) == 0 && (y & 1) == 0;
      else on = !((x & 1) == 0 && (y & 1) == 0);
      if (on) c.put(x, y, 255);
    }
}

void braille(Canvas& c, unsigned pattern) {
  // dot numbering: 1 2 3 7 down the left column, 4 5 6 8 down the right; bits 0..7 of the codepoint
  static constexpr int kCol[8] = {0, 0, 0, 1, 1, 1, 0, 1};
  static constexpr int kRow[8] = {0, 1, 2, 0, 1, 2, 3, 3};
  const int d = std::max(1, c.w / 4);
  for (int i = 0; i < 8; ++i)
    if (pattern & (1u << i)) {
      const int cx = rnd((kCol[i] + 0.5) * c.w / 2.0), cy = rnd((kRow[i] + 0.5) * c.h / 4.0);
      c.rect(cx - d / 2, cy - d / 2, cx - d / 2 + d, cy - d / 2 + d);
    }
}

// Box drawing: styles per arm are 0 none, 1 light, 2 heavy
struct Arms { int up, right, down, left; };

void boxArms(Canvas& c, Arms a) {
  const int tl = std::max(1, c.h / 16), th = tl * 2;
  const int xc = c.w / 2, yc = c.h / 2;
  auto hseg = [&](int style, int x0, int x1) {
    if (!style) return;
    const int t = style == 2 ? th : tl;
    c.rect(x0, yc - t / 2, x1, yc - t / 2 + t);
  };
  auto vseg = [&](int style, int y0, int y1) {
    if (!style) return;
    const int t = style == 2 ? th : tl;
    c.rect(xc - t / 2, y0, xc - t / 2 + t, y1);
  };
  const int jx = std::max(a.up == 2 || a.down == 2 ? th : tl, 1), jy = std::max(a.left == 2 || a.right == 2 ? th : tl, 1);
  hseg(a.left, 0, xc + jx / 2 + 1);
  hseg(a.right, xc - jx / 2, c.w);
  vseg(a.up, 0, yc + jy / 2 + 1);
  vseg(a.down, yc - jy / 2, c.h);
}

void doubleLines(Canvas& c, bool left, bool right, bool up, bool down) {
  // ═ ║ and the four double corners; strokes are tl thick, separated by a gap
  const int tl = std::max(1, c.h / 16), gap = std::max(2, tl * 2), off = (gap + tl) / 2 + 0;
  const int xc = c.w / 2, yc = c.h / 2;
  auto hs = [&](int y, int x0, int x1) { c.rect(x0, y - tl / 2, x1, y - tl / 2 + tl); };
  auto vs = [&](int x, int y0, int y1) { c.rect(x - tl / 2, y0, x - tl / 2 + tl, y1); };
  const bool horiz = left || right, vert = up || down;
  if (horiz && !vert) {
    hs(yc - off, 0, c.w);
    hs(yc + off, 0, c.w);
  } else if (vert && !horiz) {
    vs(xc - off, 0, c.h);
    vs(xc + off, 0, c.h);
  } else {
    const int hd = right ? 1 : -1, vd = down ? 1 : -1;  // directions the corner opens toward
    auto hrange = [&](int fromX, int y) { hd > 0 ? hs(y, fromX - tl / 2, c.w) : hs(y, 0, fromX + tl / 2 + 1); };
    auto vrange = [&](int x, int fromY) { vd > 0 ? vs(x, fromY - tl / 2, c.h) : vs(x, 0, fromY + tl / 2 + 1); };
    hrange(xc - hd * off, yc - vd * off);  // outer horizontal
    vrange(xc - hd * off, yc - vd * off);  // outer vertical
    hrange(xc + hd * off, yc + vd * off);  // inner horizontal
    vrange(xc + hd * off, yc + vd * off);  // inner vertical
  }
}

void diagonal(Canvas& c, bool slash, bool backslash) {
  const double thick = std::max(1.0, c.h / 16.0) * 1.0;
  const double norm = std::sqrt(static_cast<double>(c.w) * c.w + static_cast<double>(c.h) * c.h);
  for (int y = 0; y < c.h; ++y)
    for (int x = 0; x < c.w; ++x) {
      const double u = (x + 0.5) / c.w, v = (y + 0.5) / c.h;
      double best = 1e9;
      if (slash) best = std::min(best, std::abs(v - (1.0 - u)) * c.w * c.h / norm);
      if (backslash) best = std::min(best, std::abs(v - u) * c.w * c.h / norm);
      const double cov = std::clamp(thick / 2 + 0.5 - best, 0.0, 1.0);
      if (cov > 0) c.put(x, y, static_cast<std::uint8_t>(cov * 255));
    }
}

}  // namespace

struct AtlasBuilder {
  int cw, ch;
  Atlas atlas;
  Canvas canvas;
  std::set<char32_t> done;

  AtlasBuilder(int w, int h) : cw(w), ch(h), atlas(w, h), canvas(w, h) {}

  void upload(char32_t cp) {
    atlas.set(cp, canvas.a.data());
    done.insert(cp);
  }
  template <class F> void make(char32_t cp, F&& draw) {
    canvas.clear();
    draw(canvas);
    upload(cp);
  }

  void text(const FontFace& face) {
    if (!face.loaded()) return;
    stbtt_fontinfo* info = &face.impl_->info;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    int adv0, lsb0;
    stbtt_GetCodepointHMetrics(info, 'M', &adv0, &lsb0);
    // fit the font's line height into the cell, and its advance into the cell width
    double scale = std::min(static_cast<double>(ch) / (asc - desc), adv0 > 0 ? static_cast<double>(cw) / adv0 : 1e9);
    const double lineH = (asc - desc) * scale;
    const int baseline = rnd((ch - lineH) / 2.0 + asc * scale);
    static constexpr std::array<std::array<char32_t, 2>, 13> kRanges = {{
        {0x20, 0x7E}, {0xA0, 0x17F}, {0x2010, 0x2027}, {0x2030, 0x203A}, {0x20AC, 0x20AC}, {0x2122, 0x2122},
        {0x2190, 0x21FF}, {0x2200, 0x22FF}, {0x2460, 0x24FF}, {0x25A0, 0x25FF}, {0x2600, 0x26FF}, {0x2700, 0x27BF},
        {0x2B00, 0x2B1F}}};
    std::vector<unsigned char> bmp;
    for (const auto& r : kRanges)
      for (char32_t cp = r[0]; cp <= r[1]; ++cp) {
        if (done.count(cp)) continue;  // an earlier (preferred) face already supplied it
        const int g = stbtt_FindGlyphIndex(info, static_cast<int>(cp));
        if (g == 0) continue;
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(info, g, static_cast<float>(scale), static_cast<float>(scale), &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw < 0 || gh < 0) continue;
        int adv, lsb;
        stbtt_GetGlyphHMetrics(info, g, &adv, &lsb);
        const double pen = (cw - adv * scale) / 2.0;  // center the advance box in the cell
        canvas.clear();
        if (gw > 0 && gh > 0) {
          bmp.assign(static_cast<size_t>(gw) * static_cast<size_t>(gh), 0);
          stbtt_MakeGlyphBitmap(info, bmp.data(), gw, gh, gw, static_cast<float>(scale), static_cast<float>(scale), g);
          for (int y = 0; y < gh; ++y)
            for (int x = 0; x < gw; ++x) canvas.put(rnd(pen) + x0 + x, baseline + y0 + y, bmp[static_cast<size_t>(y) * static_cast<size_t>(gw) + static_cast<size_t>(x)]);
        }
        upload(cp);
      }
  }

  void blocks() {
    // U+2580..259F block elements
    make(0x2580, [&](Canvas& c) { c.rect(0, 0, cw, ch / 2); });
    for (int k = 1; k <= 7; ++k) make(0x2580 + k, [&](Canvas& c) { c.rect(0, ch - rnd(k * ch / 8.0), cw, ch); });
    make(0x2588, [&](Canvas& c) { c.rect(0, 0, cw, ch); });
    for (int k = 7; k >= 1; --k) make(0x2588 + (8 - k), [&](Canvas& c) { c.rect(0, 0, rnd(k * cw / 8.0), ch); });
    make(0x2590, [&](Canvas& c) { c.rect(cw / 2, 0, cw, ch); });
    make(0x2591, [&](Canvas& c) { shade(c, 1); });
    make(0x2592, [&](Canvas& c) { shade(c, 2); });
    make(0x2593, [&](Canvas& c) { shade(c, 3); });
    make(0x2594, [&](Canvas& c) { c.rect(0, 0, cw, std::max(1, rnd(ch / 8.0))); });
    make(0x2595, [&](Canvas& c) { c.rect(cw - std::max(1, rnd(cw / 8.0)), 0, cw, ch); });
    // quadrants U+2596..259F: pattern bits 1=UL 2=UR 4=LL 8=LR
    static constexpr unsigned kQuad[10] = {4, 8, 1, 1 | 4 | 8, 1 | 8, 1 | 2 | 4, 1 | 2 | 8, 2, 2 | 4, 2 | 4 | 8};
    for (int i = 0; i < 10; ++i) make(0x2596 + i, [&](Canvas& c) { mosaic(c, 2, 2, kQuad[i]); });
  }

  void sextants() {
    for (unsigned p = 1; p <= 62; ++p) {
      if (p == 21 || p == 42) continue;
      make(sextantGlyph(p), [&](Canvas& c) { mosaic(c, 2, 3, p); });
    }
  }

  void mosaics() {
    for (unsigned p = 0; p < 256; ++p) make(mosaicGlyph(p), [&](Canvas& c) { mosaic(c, 2, 4, p); });
  }

  void braillePage() {
    for (unsigned p = 0; p < 256; ++p) make(0x2800 + p, [&](Canvas& c) { braille(c, p); });
  }

  void boxes() {
    struct B { char32_t cp; Arms a; };
    // light (1) and heavy (2) arms: up, right, down, left
    static constexpr B kBoxes[] = {
        {0x2500, {0, 1, 0, 1}}, {0x2501, {0, 2, 0, 2}}, {0x2502, {1, 0, 1, 0}}, {0x2503, {2, 0, 2, 0}},
        {0x250C, {0, 1, 1, 0}}, {0x250F, {0, 2, 2, 0}}, {0x2510, {0, 0, 1, 1}}, {0x2513, {0, 0, 2, 2}},
        {0x2514, {1, 1, 0, 0}}, {0x2517, {2, 2, 0, 0}}, {0x2518, {1, 0, 0, 1}}, {0x251B, {2, 0, 0, 2}},
        {0x251C, {1, 1, 1, 0}}, {0x2523, {2, 2, 2, 0}}, {0x2524, {1, 0, 1, 1}}, {0x252B, {2, 0, 2, 2}},
        {0x252C, {0, 1, 1, 1}}, {0x2533, {0, 2, 2, 2}}, {0x2534, {1, 1, 0, 1}}, {0x253B, {2, 2, 0, 2}},
        {0x253C, {1, 1, 1, 1}}, {0x254B, {2, 2, 2, 2}},
        {0x256D, {0, 1, 1, 0}}, {0x256E, {0, 0, 1, 1}}, {0x256F, {1, 0, 0, 1}}, {0x2570, {1, 1, 0, 0}},  // rounded -> square
        {0x2574, {0, 0, 0, 1}}, {0x2575, {1, 0, 0, 0}}, {0x2576, {0, 1, 0, 0}}, {0x2577, {0, 0, 1, 0}},
    };
    for (const B& b : kBoxes) make(b.cp, [&](Canvas& c) { boxArms(c, b.a); });
    make(0x2550, [&](Canvas& c) { doubleLines(c, true, true, false, false); });
    make(0x2551, [&](Canvas& c) { doubleLines(c, false, false, true, true); });
    make(0x2554, [&](Canvas& c) { doubleLines(c, false, true, false, true); });
    make(0x2557, [&](Canvas& c) { doubleLines(c, true, false, false, true); });
    make(0x255A, [&](Canvas& c) { doubleLines(c, false, true, true, false); });
    make(0x255D, [&](Canvas& c) { doubleLines(c, true, false, true, false); });
    make(0x2571, [&](Canvas& c) { diagonal(c, true, false); });
    make(0x2572, [&](Canvas& c) { diagonal(c, false, true); });
    make(0x2573, [&](Canvas& c) { diagonal(c, true, true); });
  }
};

Atlas buildGlyphAtlas(int cellW, int cellH, const std::vector<const FontFace*>& faces, int textSixths) {
  AtlasBuilder b(cellW, cellH);
  for (const FontFace* f : faces)
    if (f) b.text(*f);
  // synthesized glyphs overwrite anything the font drew for the same codepoint
  b.boxes();
  b.blocks();
  b.braillePage();
  b.sextants();
  b.mosaics();
  // overlay text at its own pitch: the same glyph families, built for the text cell size
  const int tw = cellW * textSixths / 6, th = cellH * textSixths / 6;
  if (textSixths != 6 && tw > 0 && th > 0 && (tw != cellW || th != cellH))
    b.atlas.setTextAtlas(std::make_shared<Atlas>(buildGlyphAtlas(tw, th, faces, 6)));
  return std::move(b.atlas);
}

}  // namespace laby
