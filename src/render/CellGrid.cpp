#include "render/CellGrid.hpp"

namespace laby {

void CellGrid::box(int x, int y, int w, int h, Rgb fg, Rgb bg) {
  if (w < 2 || h < 2) return;
  for (int i = x + 1; i < x + w - 1; ++i) {
    put(i, y, U'─', fg, bg);
    put(i, y + h - 1, U'─', fg, bg);
  }
  for (int j = y + 1; j < y + h - 1; ++j) {
    put(x, j, U'│', fg, bg);
    put(x + w - 1, j, U'│', fg, bg);
  }
  put(x, y, U'┌', fg, bg);
  put(x + w - 1, y, U'┐', fg, bg);
  put(x, y + h - 1, U'└', fg, bg);
  put(x + w - 1, y + h - 1, U'┘', fg, bg);
}

namespace {
// Decodes the next UTF-8 code point of s starting at i; advances i.
char32_t nextCodepoint(std::string_view s, size_t& i) {
  unsigned char c = static_cast<unsigned char>(s[i]);
  char32_t cp;
  int len;
  if (c < 0x80) { cp = c; len = 1; }
  else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
  else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
  else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
  else { cp = U'?'; len = 1; }
  for (int k = 1; k < len && i + static_cast<size_t>(k) < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0x3F);
  i += static_cast<size_t>(len);
  return cp;
}
}  // namespace

int CellGrid::printText(int cx, int cy, int col, int row, std::string_view s, Rgb fg, float scale, float dy) {
  for (size_t i = 0; i < s.size();) {
    runs_.push_back(TextRun{cx, cy, col++, row, nextCodepoint(s, i), fg, scale, dy});
  }
  return col;
}

int CellGrid::print(int x, int y, std::string_view s, Rgb fg, Rgb bg) {
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    char32_t cp;
    int len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
    else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
    else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
    else { cp = U'?'; len = 1; }
    for (int k = 1; k < len && i + k < s.size(); ++k)
      cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    put(x++, y, cp, fg, bg);
    i += len;
  }
  return x;
}

}  // namespace laby
