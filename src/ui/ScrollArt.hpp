#pragma once
#include "platform/Theme.hpp"
#include "render/CellGrid.hpp"

namespace laby {

// Procedural scroll pictures for the "A Scroll!" chest panel, drawn into the cells x..x+cols-1, y..y+rows-1 (2x4 sub-pixels
// each, so square). Flat theme tones, light from the right, a gentle animation on `t` (seconds since the chest opened).
struct ScrollColors {
  Rgb paper;      // the panel's background
  Rgb parchment;  // the scroll's paper
  Rgb ink;        // text and outlines on it, and what the shading darkens towards (the panel background on a dark theme, the text color on a light one)
  Rgb glint;      // what sparkles and rings over the bare paper brighten towards: white on a dark theme, dark on a light one (white would vanish)
  Rgb glow;       // the magic around it
  Rgb seal;       // wax and ribbon
};
// The scroll's colors from the theme: parchment from the coin and text tones (on a light theme the coin tint over the background, so it stays
// light), wax from the danger role, the magic glow in the accent.
inline ScrollColors scrollColors(const Theme& th) {
  const Rgb parchment = th.light ? mix(th.coin, th.bg, 0.55) : mix(th.coin, th.fg, 0.55);
  return {th.bg, parchment, th.light ? th.fg : th.bg, th.light ? Rgb{0, 0, 0} : Rgb{255, 255, 255}, th.accent, th.danger};
}
constexpr int kScrollVariants = 4;
//  0 Sheet: an unrolled sheet between two rolls, lines of text, a wax seal, sparks.
//  1 Tied: a rolled scroll lying at an angle, ribbon bow and seal, rays behind it.
//  2 Circle: the open sheet floating over a slowly turning rune circle and hexagram.
//  3 Unfurl: the sheet rolls open with a burst, its runes drifting up off the page.
void drawScroll(CellGrid& g, int x, int y, int cols, int rows, int variant, const ScrollColors& pal, double t);

}  // namespace laby
