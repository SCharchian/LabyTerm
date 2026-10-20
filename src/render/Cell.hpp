#pragma once
#include <cstdint>

namespace laby {

struct Rgb {
  std::uint8_t r = 0, g = 0, b = 0;
  constexpr bool operator==(const Rgb&) const = default;
};

// One character cell: a Unicode glyph plus foreground/background colors.
// All game drawing is expressed in cells; backends decide how to show them.
struct Cell {
  char32_t ch = U' ';
  Rgb fg{200, 200, 200};
  Rgb bg{0, 0, 0};
  std::uint8_t glow = 0;  // 1: this cell shows the ink of a 3D object, which the CRT glow lights up whatever its brightness (see GlowMask); 2: the same, drawn as a text character (which inks only part of its cell, so its glow is boosted)
  constexpr bool operator==(const Cell&) const = default;
};

}  // namespace laby
