#include "render/TextShapes.hpp"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <vector>

#include "core/DataFile.hpp"
#include "render/GlyphAtlas.hpp"

namespace laby {
namespace {

constexpr int kRefW = 16, kRefH = 32;  // the size the characters are rasterized at to measure them
constexpr int kSw = 4, kSh = 8;        // samples across and down a cell
static_assert(kSw * kSh == kTextSamples);
constexpr double kInkGain = 2.0;       // strokes are thin: a character with half a sample inked counts as lit
// The characters on offer: symbols, which read as shapes, and the few letters that do too. Most letters are left out; at this
// size they would just look like noise. Latin-1 symbols can be added to kLatin1Symbols (chosen with tests/text_charset_pick), but
// none is in: over real views the thin ones (a broken bar, a top edge, a mid dot) lower the matching error by under 1% in all, since
// ASCII has `.` `,` `-` `'` `_` already, and the ones that do lower it (the copyright-style signs, the pilcrow, the micro sign) are busy
// shapes that fill flat walls with noise instead of the even '@'.
const char32_t kCharset[] = U".,:;'\"`-_=+*~^!|/\\()[]{}<>#%&@$?oOxXIli";
const char32_t kLatin1Symbols[] = {0};

using Samples = TextSamples;

// A soft copy (weights 1 2 1 across and down, edges repeated). Characters leave a margin at the sides of the cell, so a bar
// in the outermost sub-pixels would match nothing exactly; compared softly it still lands on the character nearest to it.
Samples soften(const Samples& in) {
  static constexpr float w[3] = {0.15f, 0.7f, 0.15f};
  Samples out{};
  for (int j = 0; j < kSh; ++j)
    for (int i = 0; i < kSw; ++i) {
      float v = 0;
      for (int dj = -1; dj <= 1; ++dj)
        for (int di = -1; di <= 1; ++di)
          v += w[dj + 1] * w[di + 1] * in[static_cast<size_t>(std::clamp(j + dj, 0, kSh - 1) * kSw + std::clamp(i + di, 0, kSw - 1))];
      out[static_cast<size_t>(j * kSw + i)] = v;
    }
  return out;
}

struct Shapes {
  std::vector<TextShape> shapes;
  bool fromFont = false;
};

std::vector<TextShape> measure(const std::u32string& chars) {
  std::vector<TextShape> out;
  FontFace face;
  if (!face.load(dataDir() + "/fonts/AdwaitaMono-Regular.ttf")) return out;
  const Atlas atlas = buildGlyphAtlas(kRefW, kRefH, {&face}, 6);
  for (const char32_t c : chars) {
    const std::uint8_t* t = atlas.tile(c);
    if (!t) continue;
    Samples cov{};
    const int bw = kRefW / kSw, bh = kRefH / kSh;
    for (int j = 0; j < kSh; ++j)
      for (int i = 0; i < kSw; ++i) {
        long sum = 0;
        for (int y = 0; y < bh; ++y)
          for (int x = 0; x < bw; ++x) sum += t[(j * bh + y) * kRefW + i * bw + x];
        cov[static_cast<size_t>(j * kSw + i)] = static_cast<float>(std::min(1.0, kInkGain * static_cast<double>(sum) / (255.0 * bw * bh)));
      }
    out.push_back({c, soften(cov)});
  }
  return out;
}

Shapes build() {
  Shapes s;
  std::u32string chars(kCharset);
  chars += kLatin1Symbols;
  s.shapes = measure(chars);
  s.fromFont = !s.shapes.empty();
  return s;
}

void (*gObserver)(std::uint32_t) = nullptr;

}  // namespace

std::vector<TextShape> measureTextShapes(const std::u32string& chars) { return measure(chars); }

TextSamples softenedPattern(std::uint32_t pattern) {
  Samples lit{};
  for (int k = 0; k < kTextSamples; ++k) lit[static_cast<size_t>(k)] = pattern >> k & 1u ? 1.0f : 0.0f;
  return soften(lit);
}

float textShapeError(const TextSamples& pat, const TextShape& shape) {
  float e = 0;
  for (int k = 0; k < kTextSamples; ++k) {
    const float d = pat[static_cast<size_t>(k)] - shape.cover[static_cast<size_t>(k)];
    e += d * d;
  }
  return e;
}

void setTextPatternObserver(void (*observer)(std::uint32_t)) { gObserver = observer; }

char32_t bestTextGlyph(std::uint32_t pattern) {
  if (gObserver) gObserver(pattern);
  if (!pattern) return U' ';
  // Large flat areas (a wall face, a stretch of floor) repeat the same few patterns, so each answer is worked out once.
  static thread_local std::unordered_map<std::uint32_t, char32_t> cache;
  const auto hit = cache.find(pattern);
  if (hit != cache.end()) return hit->second;
  static const Shapes shapes = build();
  char32_t pick;
  if (!shapes.fromFont) {  // a density ramp by the number of lit pixels
    static const char kRamp[] = ".:-=+*#%@";
    int n = 0;
    for (std::uint32_t b = pattern; b; b &= b - 1) ++n;
    pick = static_cast<char32_t>(kRamp[(n - 1) * 8 / (kTextSamples - 1)]);
  } else {
    const Samples lit = softenedPattern(pattern);
    float best = 1e9f;
    pick = U'#';
    for (const TextShape& s : shapes.shapes) {
      const float e = textShapeError(lit, s);
      if (e < best) { best = e; pick = s.ch; }
    }
  }
  if (cache.size() > 200000) cache.clear();  // (patterns from a dither are many: keep it bounded)
  cache.emplace(pattern, pick);
  return pick;
}

}  // namespace laby
