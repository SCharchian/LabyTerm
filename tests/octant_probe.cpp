// Run this in a terminal to see whether it can show LabyTerm's mosaics: truecolor, Unicode 16 octants (and the
// sextant / quadrant / half-block fallbacks), and that the glyphs are one cell wide. No game code needed.
//   build/octant_probe
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "render/TermOut.hpp"

using namespace laby;

namespace {

void out(const std::string& s) { fwrite(s.data(), 1, s.size(), stdout); fflush(stdout); }

// Asks the terminal where the cursor is (DSR 6); returns the column, or -1 if it does not answer.
int cursorColumn() {
  if (!isatty(STDIN_FILENO)) return -1;
  termios old{};
  tcgetattr(STDIN_FILENO, &old);
  termios raw = old;
  raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  out("\x1b[6n");
  std::string reply;
  pollfd p{STDIN_FILENO, POLLIN, 0};
  while (poll(&p, 1, 300) > 0) {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) break;
    reply += c;
    if (c == 'R') break;
  }
  tcsetattr(STDIN_FILENO, TCSANOW, &old);
  int row = 0, col = 0;
  return sscanf(reply.c_str(), "\x1b[%d;%dR", &row, &col) == 2 ? col : -1;
}

// A 2*cols x 4*rows picture of a ring and a diagonal; true = ink.
struct Pic {
  int w, h;
  std::vector<bool> px;
  bool at(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && px[static_cast<size_t>(y) * w + x]; }
};
Pic makePic(int cols, int rows) {
  Pic p{cols * 2, rows * 4, {}};
  p.px.assign(static_cast<size_t>(p.w) * p.h, false);
  const double cx = p.w / 2.0, cy = p.h / 2.0, r = std::min(p.w, p.h) / 2.0 - 0.5;
  for (int y = 0; y < p.h; ++y)
    for (int x = 0; x < p.w; ++x) {
      const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
      p.px[static_cast<size_t>(y) * p.w + x] = (d <= r && d >= r - 2.0) || (x == y * p.w / p.h) || (x + y * p.w / p.h == p.w - 1);
    }
  return p;
}

// Draws the picture with `sw` x `sh` sub-pixels per cell, sampling the 2x4 picture (so every mode shows the same shape).
std::string render(const Pic& pic, int sw, int sh, Rgb ink, Rgb paper) {
  std::string s;
  const int cols = pic.w / 2, rows = pic.h / 4;
  for (int row = 0; row < rows; ++row) {
    appendFg(s, ink);
    appendBg(s, paper);
    for (int col = 0; col < cols; ++col) {
      unsigned pat = 0;
      for (int j = 0; j < sh; ++j)
        for (int i = 0; i < sw; ++i) {
          const int x = (col * sw + i) * 2 / sw, y = (row * sh + j) * 4 / sh;  // the picture pixel this sub-pixel looks at
          if (pic.at(x + (pic.w - cols * 2) / 2, y)) pat |= 1u << (j * sw + i);
        }
      if (sw == 2 && sh == 4) appendUtf8(s, kOctantChar[pat]);
      else if (sw == 2 && sh == 3) appendUtf8(s, sextantChar(pat));
      else if (sw == 2 && sh == 2) {
        static constexpr char32_t q[16] = {U' ', 0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B,
                                           0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588};
        appendUtf8(s, q[pat]);
      } else {
        appendUtf8(s, pat == 0 ? U' ' : pat == 3 ? 0x2588 : pat == 1 ? 0x2580 : 0x2584);
      }
    }
    s += "\x1b[0m\n";
  }
  return s;
}

}  // namespace

int main() {
  const char* ct = std::getenv("COLORTERM");
  const bool ctTrue = ct && (!std::strcmp(ct, "truecolor") || !std::strcmp(ct, "24bit"));
  out("LabyTerm terminal probe\n=======================\n");
  out(std::string("TERM=") + (std::getenv("TERM") ? std::getenv("TERM") : "?") + "  TERM_PROGRAM=" +
      (std::getenv("TERM_PROGRAM") ? std::getenv("TERM_PROGRAM") : "?") + "  COLORTERM=" + (ct ? ct : "(unset)") +
      (ctTrue ? "  -> claims truecolor\n" : "  -> does NOT claim truecolor\n"));

  out("\n1. Truecolor: this bar must be a smooth gradient, not steps or bands:\n");
  std::string bar;
  for (int i = 0; i < 64; ++i) {
    appendBg(bar, Rgb{static_cast<std::uint8_t>(i * 4), static_cast<std::uint8_t>(255 - i * 4), 128});
    bar += ' ';
  }
  out(bar + "\x1b[0m\n");

  out("\n2. Width: 16 octants must advance the cursor exactly 16 cells.\n");
  std::string probe = "\r";
  for (int i = 0, n = 0; n < 16; ++i)
    if (kOctantChar[i] >= 0x1CD00) {  // the first 16 true octants (U+1CD00..)
      appendUtf8(probe, kOctantChar[i]);
      ++n;
    }
  out(probe);
  const int col = cursorColumn();
  out("\r\x1b[K");
  if (col < 0) out("   (the terminal did not answer the cursor query; judge by eye)\n");
  else out(col == 17 ? "   OK: cursor moved 16 cells\n" : "   PROBLEM: cursor is at column " + std::to_string(col) + ", expected 17\n");

  const Rgb ink{240, 200, 90}, paper{20, 24, 40};
  const Pic pic = makePic(16, 8);
  out("\n3. The same ring and X, in four mosaic styles. The octant one (left) must be the sharpest;\n"
      "   if it shows boxes, question marks or gaps, this terminal/font has no octants.\n\n");
  const std::string o = render(pic, 2, 4, ink, paper), s6 = render(pic, 2, 3, ink, paper), q = render(pic, 2, 2, ink, paper),
                    h = render(pic, 1, 2, ink, paper);
  auto lines = [](const std::string& t) {
    std::vector<std::string> v;
    size_t a = 0;
    for (size_t b; (b = t.find('\n', a)) != std::string::npos; a = b + 1) v.push_back(t.substr(a, b - a));
    return v;
  };
  const auto lo = lines(o), ls = lines(s6), lq = lines(q), lh = lines(h);
  out("   octants 2x4       sextants 2x3      quadrants 2x2     half blocks 1x2\n");
  for (size_t i = 0; i < lo.size(); ++i) out("   " + lo[i] + "     " + ls[i] + "     " + lq[i] + "     " + lh[i] + "\n");

  out("\n4. All 256 octant patterns (16 per row). Look for tofu boxes, '?' or empty gaps:\n\n");
  for (int r = 0; r < 16; ++r) {
    std::string line = "   ";
    appendFg(line, ink);
    appendBg(line, paper);
    for (int c = 0; c < 16; ++c) {
      appendUtf8(line, kOctantChar[r * 16 + c]);
      line += ' ';
    }
    out(line + "\x1b[0m\n");
  }
  out("\nThe 26 patterns that are not true octants (quadrants, quarter blocks) are the usual survivors;\n"
      "the other 230 are U+1CD00..U+1CDE5 (Unicode 16). Missing ones show up as gaps in the table above.\n");
  return 0;
}
