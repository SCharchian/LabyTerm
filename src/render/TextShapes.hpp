#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "render/Cell.hpp"

namespace laby {

// The text look of the maze: of a cell's 4x8 sub-pixels (bit k = pixel col k % 4, row k / 4, set = inked), the printable
// character whose shape covers them best. The shapes are measured once from the bundled font's own outlines (each
// character's ink in each sub-pixel, rasterized large), so '|' answers a vertical bar, '_' a floor line, '/' a diagonal,
// '#' a solid and '.' a speck. The font the player picked draws the character; this one only decides which. If the bundled
// font is missing it falls back to a plain density ramp. Nothing set gives a space. Answers are cached per pattern.
char32_t bestTextGlyph(std::uint32_t pattern);

// ---- for tools that choose the character set (tests/text_charset_pick.cpp) --------------------------------------------
constexpr int kTextSamples = 32;
using TextSamples = std::array<float, kTextSamples>;
struct TextShape {
  char32_t ch;
  TextSamples cover;  // softened, as the matching compares them
};
std::vector<TextShape> measureTextShapes(const std::u32string& chars);  // empty if the bundled font is missing
TextSamples softenedPattern(std::uint32_t pattern);
float textShapeError(const TextSamples& softenedPattern, const TextShape& shape);
// Called with every pattern bestTextGlyph is asked about (before the cache), if set. For tools only.
void setTextPatternObserver(void (*observer)(std::uint32_t pattern));

}  // namespace laby
