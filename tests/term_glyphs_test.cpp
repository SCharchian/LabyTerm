// The octant table must give every one of the 256 mosaic patterns its own character, 230 of them true octants.
#include <cstdio>
#include <filesystem>
#include <set>
#include <unistd.h>

#include "render/TermCaps.hpp"
#include "render/TermOut.hpp"

int main() {
  using namespace laby;
  std::set<char32_t> seen;
  int octants = 0, fails = 0;
  auto check = [&](bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++fails; } };
  for (unsigned p = 0; p < 256; ++p) {
    check(seen.insert(kOctantChar[p]).second, "every pattern has its own character");
    octants += kOctantChar[p] >= 0x1CD00 && kOctantChar[p] <= 0x1CDE5;
    check(termGlyph(0xE000 + p) == kOctantChar[p], "the game's private-use mosaic glyph maps to the table");
  }
  check(octants == 230, "230 patterns are true Unicode octants");
  check(kOctantChar[0] == U' ' && kOctantChar[255] == 0x2588, "blank and full");
  check(kOctantChar[1 | 4] == 0x2598 && kOctantChar[0xF0] == 0x2584 && kOctantChar[0x0F] == 0x2580, "quadrants and halves");
  check(kOctantChar[4] == 0x1CD00, "pattern 'cell 3' is the first octant");
  check(termGlyph(U'A') == U'A' && termGlyph(0x2500) == 0x2500, "other characters pass through");
  check(sextantChar(1) == 0x1FB00 && sextantChar(62) == 0x1FB3B && sextantChar(21) == 0x258C, "sextants");
  std::string s;
  appendUtf8(s, 0x1CD00);
  check(s == "\xF0\x9C\xB4\x80", "UTF-8 of U+1CD00");
  // quadrant fallback: a quadrant is inked when either of its octant rows is
  check(quadrantChar(0) == U' ' && quadrantChar(0xFF) == 0x2588, "quadrants: blank and full");
  check(quadrantChar(0x01) == 0x2598 && quadrantChar(0x04) == 0x2598, "quadrants: either octant row inks the upper left");
  check(quadrantChar(0x0F) == 0x2580 && quadrantChar(0xF0) == 0x2584 && quadrantChar(0x55) == 0x258C && quadrantChar(0xAA) == 0x2590, "quadrants: halves");
  check(termGlyph(0xE000 + 0x0F, false) == 0x2580 && termGlyph(0xE000 + 0x0F, true) == kOctantChar[0x0F] && termGlyph(U'A', false) == U'A', "termGlyph by mode");

  // the startup queries' answers
  {
    const TermReplies a = parseTermReplies("\x1bP1$r0;48:2::1:2:3m\x1b\\\x1bP>|ghostty 1.3.1\x1b\\\x1b[?62;22c");
    check(a.truecolorSgr && a.xtversion == "ghostty 1.3.1" && a.answered, "replies: colon form, version, device attributes");
    const TermReplies b = parseTermReplies("\x1bP1$r0;48;2;1;2;3m\x1b\\\x1b[?1;2c");
    check(b.truecolorSgr && b.xtversion.empty() && b.answered, "replies: semicolon form");
    const TermReplies c = parseTermReplies("\x1bP0$r\x1b\\\x1b[?6c");
    check(!c.truecolorSgr && c.answered, "replies: an invalid answer is not truecolor");
    const TermReplies d = parseTermReplies("\x1bP1$r0;48;5;17m\x1b\\\x1bP>|foot(1.28.0)\x1b\\\x1b[?62c");
    check(!d.truecolorSgr && d.xtversion == "foot(1.28.0)", "replies: a 256-colour echo is not truecolor");
    check(!parseTermReplies("").answered && !parseTermReplies("garbage").answered, "replies: nothing, or noise");
    check(truecolorLikely("truecolor", {}) && truecolorLikely("24bit", {}) && !truecolorLikely("", {}) && truecolorLikely("", a), "truecolor decision");
    check(terminalId(a, "x", "y") == "ghostty 1.3.1" && terminalId({}, "", "xterm-256color") == "?/xterm-256color" && terminalId({}, "WezTerm", "xterm-256color") == "WezTerm/xterm-256color", "terminal id");
    check(knownOctantTerminal("ghostty 1.3.1") && knownOctantTerminal("foot(1.28.0)") && knownOctantTerminal("Ghostty 1.3.1") && !knownOctantTerminal("?/xterm-256color") && !knownOctantTerminal("kitty(0.40)"), "known octant terminals");
  }
  check(tmuxTextHasRgb("bpaste,ccolour,clipboard,cstyle,focus,RGB,title") && tmuxTextHasRgb(",xterm*:Tc") && tmuxTextHasRgb("xterm*:RGB:clipboard") &&
            !tmuxTextHasRgb("bpaste,ccolour,focus,title") && !tmuxTextHasRgb("") && !tmuxTextHasRgb("xterm*:clipboard:ccolour:cstyle:focus:title screen*:title"),
        "tmux: RGB or Tc in the features or overrides");
  // the remembered answer is per terminal
  {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("labyterm_caps_test_" + std::to_string(getpid()));
    std::filesystem::remove_all(dir);
    check(!loadGlyphChoice(dir.string(), "t1"), "choice: nothing saved yet");
    saveGlyphChoice(dir.string(), "t1", GlyphMode::Quadrant);
    check(loadGlyphChoice(dir.string(), "t1") == GlyphMode::Quadrant, "choice: saved and loaded");
    check(!loadGlyphChoice(dir.string(), "t2"), "choice: another terminal is asked again");
    saveGlyphChoice(dir.string(), "t2", GlyphMode::Octant);
    check(loadGlyphChoice(dir.string(), "t2") == GlyphMode::Octant && !loadGlyphChoice(dir.string(), "t1"), "choice: the latest terminal's answer replaces the old one");
    std::filesystem::remove_all(dir);
  }
  std::printf(fails ? "term_glyphs_test: %d failure(s)\n" : "term_glyphs_test: ok\n", fails);
  return fails != 0;
}
