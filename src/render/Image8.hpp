#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace laby {

// A smooth two-tone picture with transparency: per pixel, `alpha` (0 = transparent .. 255 = opaque) and
// `tone` (0 = the dark color .. 255 = the light color). Stored as 2-channel PNGs (R = tone, A = alpha)
// under data/art and drawn as real pixels, not as glyph cells.
struct Image8 {
  int w = 0, h = 0;
  std::vector<std::uint8_t> tone, alpha;
  bool empty() const { return w == 0; }
  // PNG with RGBA: R = tone, A = alpha. Empty image if the file cannot be read.
  static Image8 load(const std::string& path);
  bool save(const std::string& path) const;
};

}  // namespace laby
