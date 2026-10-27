#pragma once
// What the terminal build can learn about the terminal it runs in, and what it does about it.
//
// Truecolor: COLORTERM says so, or the terminal confirms it when asked (DECRQSS). Octant glyphs (Unicode 16): a program cannot see whether a
// glyph was drawn, so terminals known to draw them are accepted, and any other terminal is asked once ("do these look right?") and
// remembered. Without octants the mosaics fall back to quadrant characters, which every font has.
#include <optional>
#include <string>

namespace laby {

enum class GlyphMode { Octant, Quadrant };

// The answers to the startup queries (TermQueries): the terminal's name and version, whether it echoed a truecolor colour back, and
// whether it answered the last query at all (primary device attributes: every terminal does), which ends the wait.
struct TermReplies {
  std::string xtversion;  // e.g. "ghostty 1.3.1" or "foot(1.28.0)"; empty if it did not say
  bool truecolorSgr = false;
  bool answered = false;
};

// The bytes to send: set a truecolor background, ask what the terminal thinks the SGR state is (DECRQSS), reset, ask for the name and
// version (XTVERSION), then ask for the device attributes, which ends the replies.
std::string termQueries();
TermReplies parseTermReplies(const std::string& bytes);

// COLORTERM (as in the environment) or a confirmed echo means truecolor.
bool truecolorLikely(const std::string& colorterm, const TermReplies& r);
// Inside tmux the colour query never reaches the real terminal, but tmux knows what it can pass on: its client_termfeatures
// ("bpaste,ccolour,RGB,title") or terminal-overrides (",xterm*:Tc") name RGB (or the older Tc) when true colour gets through.
bool tmuxTextHasRgb(const std::string& text);
// A stable name for the terminal: its own version string, else TERM_PROGRAM and TERM.
std::string terminalId(const TermReplies& r, const std::string& termProgram, const std::string& term);
// Terminals confirmed to draw the octant characters themselves (by the octant probe, tests/octant_probe.cpp).
bool knownOctantTerminal(const std::string& id);

// The remembered answer for this terminal (<dir>/terminal.json), if any.
std::optional<GlyphMode> loadGlyphChoice(const std::string& dir, const std::string& id);
void saveGlyphChoice(const std::string& dir, const std::string& id, GlyphMode mode);

// Sample pictures for the check screens, as escape-sequence text, one string per row.
std::string colorSwatch(int cells);
std::string sampleRing(GlyphMode mode, int cols, int rows, int row);

}  // namespace laby
