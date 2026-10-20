#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace laby {

// A TrueType font used to rasterize the *text* glyphs (letters, digits, punctuation, symbols).
// Everything geometric (blocks, shades, box drawing, braille, sextants, mosaics) is synthesized and
// never comes from a font, so it is pixel-exact at any cell size.
class FontFace {
 public:
  FontFace();
  ~FontFace();
  FontFace(FontFace&&) noexcept;
  FontFace& operator=(FontFace&&) noexcept;

  bool load(const std::string& path);
  bool loaded() const;
  const std::string& path() const { return path_; }
  const std::string& name() const { return name_; }

 private:
  friend struct AtlasBuilder;
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string path_, name_;
};

// A system font offered to the user (found with fontconfig).
struct FontCandidate {
  std::string label;  // e.g. "JetBrainsMono Nerd Font"
  std::string path;
};
// Fonts to offer, default first: the bundled Adwaita Mono in `bundledDir` (if present), then the system
// monospace font and a few known good terminal fonts found with fontconfig.
std::vector<FontCandidate> findFonts(const std::string& bundledDir);

// Mosaic glyphs: a 2x4 grid of "pixels" per cell. Bit i of `pattern` is pixel (col = i % 2, row = i / 2),
// i.e. 1 = top-left, 2 = top-right, 4 = second row left, ... 128 = bottom-right.
// They live in the Private Use Area so they never collide with text.
constexpr char32_t mosaicGlyph(unsigned pattern) { return static_cast<char32_t>(0xE000 + (pattern & 0xFF)); }

// The real Unicode "sextant" block character for a 2x3 pattern (bits: 1=UL 2=UR 4=ML 8=MR 16=LL 32=LR);
// blank, full, left-half and right-half map to space, FULL BLOCK, LEFT/RIGHT HALF BLOCK.
char32_t sextantGlyph(unsigned pattern);

// The real Unicode quadrant character for a 2x2 pattern (bits: 1=UL 2=UR 4=LL 8=LR).
char32_t quadrantGlyph(unsigned pattern);

// A set of glyph tiles: for each codepoint, cellW x cellH bytes of coverage (0 = background, 255 = full
// foreground). Colors are applied when a frame is rendered (FrameRenderer).
class Atlas {
 public:
  Atlas() = default;
  Atlas(int cellW, int cellH) : cw_(cellW), ch_(cellH) {}
  int cellW() const { return cw_; }
  int cellH() const { return ch_; }
  size_t size() const { return index_.size(); }
  // Coverage of the glyph for `cp`, or nullptr if the atlas has none.
  const std::uint8_t* tile(char32_t cp) const {
    const auto it = index_.find(cp);
    return it == index_.end() ? nullptr : data_.data() + it->second;
  }
  // The atlas for overlay text (TextRun): the same glyph families at the text pitch, which is the cell size scaled
  // by textSixths / 6 (see buildGlyphAtlas). When the text size equals the cell size this is the atlas itself.
  const Atlas& textAtlas() const { return text_ ? *text_ : *this; }
  void setTextAtlas(std::shared_ptr<Atlas> a) { text_ = std::move(a); }
  void set(char32_t cp, const std::uint8_t* coverage) {
    const size_t n = static_cast<size_t>(cw_) * static_cast<size_t>(ch_);
    auto it = index_.find(cp);
    if (it == index_.end()) {
      it = index_.emplace(cp, data_.size()).first;
      data_.resize(data_.size() + n);
    }
    std::copy(coverage, coverage + n, data_.begin() + static_cast<std::ptrdiff_t>(it->second));
  }

 private:
  int cw_ = 0, ch_ = 0;
  std::vector<std::uint8_t> data_;
  std::unordered_map<char32_t, size_t> index_;
  std::shared_ptr<Atlas> text_;
};

// Builds the full atlas for cells of cellW x cellH pixels. Text glyphs come from `faces` in order:
// each codepoint is taken from the first face that has it (so later faces are fallbacks for symbols the
// first one lacks). The list may be empty or hold unloaded faces (then only synthesized glyphs exist).
//
// `textSixths` is the text size in sixths of the cell size: 6 = 1x (text pitch equals the cell), 9 = 1.5x,
// 11 = 1.83x, 12 = 2x. The text pitch is floor(cellW * n / 6) x floor(cellH * n / 6) pixels per character
// (11 x 22 px at 6 x 12 cells for n = 11), so a panel that is `cols` cells wide holds floor(cols * 6 / n)
// characters per line at any cell size.
Atlas buildGlyphAtlas(int cellW, int cellH, const std::vector<const FontFace*>& faces, int textSixths = 6);

}  // namespace laby
