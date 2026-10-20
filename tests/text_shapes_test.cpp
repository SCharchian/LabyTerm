// The Text maze style: bestTextGlyph picks the character whose shape fits the cell's 4x8 sub-pixels.
//   text_shapes_test        (checks a few unmistakable shapes, then prints what the others get)
#include <cstdio>
#include <cstdlib>

#include "render/TextShapes.hpp"

namespace {
int failures = 0;
std::uint32_t rows(std::uint32_t r0, std::uint32_t r1, std::uint32_t r2, std::uint32_t r3, std::uint32_t r4, std::uint32_t r5, std::uint32_t r6, std::uint32_t r7) {
  const std::uint32_t r[8] = {r0, r1, r2, r3, r4, r5, r6, r7};
  std::uint32_t p = 0;
  for (int j = 0; j < 8; ++j) p |= r[j] << (j * 4);
  return p;
}
void expect(const char* name, std::uint32_t p, char want) {
  const char got = static_cast<char>(laby::bestTextGlyph(p));
  std::printf("%-16s %08x -> '%c'%s\n", name, p, got, want && got != want ? "   <-- FAIL" : "");
  if (want && got != want) ++failures;
}
}  // namespace

int main() {
  expect("empty", 0, ' ');
  expect("solid", 0xFFFFFFFFu, '@');
  expect("middle rows", rows(0, 0, 0, 15, 15, 0, 0, 0), '=');
  expect("slash", rows(8, 8, 4, 4, 2, 2, 1, 1), '/');
  expect("backslash", rows(1, 1, 2, 2, 4, 4, 8, 8), '\\');
  expect("left half", rows(3, 3, 3, 3, 3, 3, 3, 3), '[');
  expect("right half", rows(12, 12, 12, 12, 12, 12, 12, 12), ']');
  expect("one sample", 1, 0);  // (just shown)
  expect("vertical bar", rows(6, 6, 6, 6, 6, 6, 6, 6), 0);
  expect("left column", rows(1, 1, 1, 1, 1, 1, 1, 1), 0);
  expect("bottom row", rows(0, 0, 0, 0, 0, 0, 0, 15), 0);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
