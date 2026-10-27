#pragma once
// Helpers for writing cells to a terminal: UTF-8, truecolor escapes, and the game-glyph -> Unicode mapping.
#include <cstdint>
#include <string>

#include "render/Cell.hpp"
#include "render/TermGlyphs.hpp"

namespace laby {

inline void appendUtf8(std::string& s, char32_t c) {
  if (c < 0x80) {
    s += static_cast<char>(c);
  } else if (c < 0x800) {
    s += static_cast<char>(0xC0 | (c >> 6));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    s += static_cast<char>(0xE0 | (c >> 12));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    s += static_cast<char>(0xF0 | (c >> 18));
    s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  }
}

// SGR for truecolor foreground / background ("\x1b[38;2;R;G;Bm").
inline void appendFg(std::string& s, Rgb c) {
  s += "\x1b[38;2;" + std::to_string(c.r) + ';' + std::to_string(c.g) + ';' + std::to_string(c.b) + 'm';
}
inline void appendBg(std::string& s, Rgb c) {
  s += "\x1b[48;2;" + std::to_string(c.r) + ';' + std::to_string(c.g) + ';' + std::to_string(c.b) + 'm';
}

// The real Unicode sextant character for a 2x3 pattern (bits 1=UL 2=UR 4=ML 8=MR 16=LL 32=LR).
inline char32_t sextantChar(unsigned p) {
  p &= 63;
  if (p == 0) return U' ';
  if (p == 63) return 0x2588;  // FULL BLOCK
  if (p == 21) return 0x258C;  // LEFT HALF BLOCK
  if (p == 42) return 0x2590;  // RIGHT HALF BLOCK
  return 0x1FB00 + (p < 21 ? p - 1 : p < 42 ? p - 2 : p - 3);
}

// The quadrant character (U+2596..U+259F and the blocks) that best stands for a 2x4 octant pattern: a quadrant is inked when either of its two
// octant rows is, so thin lines survive. Every font has these, so this is the fallback where octants are not drawn.
inline char32_t quadrantChar(unsigned pattern) {
  static constexpr char32_t kQuad[16] = {U' ', 0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B, 0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588};
  const unsigned p = pattern & 0xFF;
  const unsigned ul = ((p >> 0) | (p >> 2)) & 1, ur = ((p >> 1) | (p >> 3)) & 1, ll = ((p >> 4) | (p >> 6)) & 1, lr = ((p >> 5) | (p >> 7)) & 1;
  return kQuad[ul | ur << 1 | ll << 2 | lr << 3];
}

// What the terminal should print for a glyph the game put in a cell: the game's private-use mosaic glyphs
// (GlyphAtlas.hpp, U+E000 + pattern) become standard octants; everything else is already real Unicode.
inline char32_t termGlyph(char32_t ch, bool octants = true) {
  if (ch >= 0xE000 && ch <= 0xE0FF) return octants ? kOctantChar[ch - 0xE000] : quadrantChar(ch - 0xE000);
  return ch;
}

}  // namespace laby
