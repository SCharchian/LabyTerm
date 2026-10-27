// A Backend that draws the cell grid in a real terminal (truecolor + Unicode octants) instead of an SDL window.
// Cells only. Overlay text is snapped to the cell grid; pictures
// (ImageRun), the CRT filter, the glow and the torch are not drawn.
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/Persist.hpp"
#include "render/Backend.hpp"
#include "render/TermCaps.hpp"
#include "render/TermFrame.hpp"
#include "render/TermOut.hpp"

namespace laby {
namespace {

volatile sig_atomic_t g_winch = 0, g_quit = 0;
void onWinch(int) { g_winch = 1; }
void onQuit(int) { g_quit = 1; }

// The terminal state to put back on exit, also from atexit (std::exit inside the game) and fatal signals.
termios g_saved{};
bool g_active = false;
void restoreTerminal() {
  if (!g_active) return;
  g_active = false;
  static constexpr char kLeave[] = "\x1b[0m\x1b[?1004l\x1b[?7h\x1b[?25h\x1b[?2026l\x1b[?1049l";
  ssize_t ignored = write(STDOUT_FILENO, kLeave, sizeof kLeave - 1);
  (void)ignored;
  tcsetattr(STDIN_FILENO, TCSANOW, &g_saved);
}

Key keyForChar(char c) {
  switch (c) {
    case 'z': return Key::A;
    case 'x': return Key::B;
    default: return Key::Other;
  }
}

class TerminalBackend final : public Backend {
 public:
  static constexpr int kMaxCols = 240, kMaxRows = 80;  // beyond this the view would only get more expensive to trace
  TerminalBackend(int gridW, int gridH) : gridW_(gridW), gridH_(gridH), start_(std::chrono::steady_clock::now()) {
    const std::string term = env("TERM");
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {  // (said here, not thrown: main() adds a hint about windows and displays)
      std::fputs("labyterm -t draws the game in a terminal, so it must be started from one (input and output cannot be redirected).\n"
                 "Run it from a graphical terminal such as Ghostty, kitty, foot, WezTerm or Alacritty.\n",
                 stderr);
      std::exit(1);
    }
    if (term.empty() || term == "dumb") {
      std::fprintf(stderr,
                   "labyterm: this terminal calls itself '%s', which cannot show colors or block characters.\n"
                   "Run it from a graphical terminal such as Ghostty, kitty, foot, WezTerm or Alacritty.\n",
                   term.empty() ? "(unset)" : term.c_str());
      std::exit(1);
    }
    tcgetattr(STDIN_FILENO, &g_saved);
    termios raw = g_saved;
    cfmakeraw(&raw);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    g_active = true;
    std::atexit(restoreTerminal);

    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);  // no SA_RESTART: a resize or a quit signal must interrupt poll()
    sa.sa_handler = onWinch;
    sigaction(SIGWINCH, &sa, nullptr);
    sa.sa_handler = onQuit;
    for (int sig : {SIGTERM, SIGHUP, SIGINT}) sigaction(sig, &sa, nullptr);

    // alternate screen, hidden cursor, no auto-wrap, focus reports
    writeAll("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[?1004h");
    measure();
    negotiate();
  }
  ~TerminalBackend() override {
    restoreTerminal();
    leaveFinalScreen();
  }

  bool pollEvent(Event& out) override { return next(out, 0); }
  bool waitEvent(Event& out, int timeoutMs) override { return next(out, timeoutMs); }
  bool needsFrame() const override { return frameWanted_; }

  void present(const CellGrid& grid) override {
    frameWanted_ = false;
    if (g_winch) measure();
    gridW_ = grid.width();
    gridH_ = grid.height();
    if (termW_ < gridW_ || termH_ < gridH_) {  // (the app sizes the grid to the terminal, so this is only the case below the minimum)
      showTooSmall();
      return;
    }
    tooSmallShown_ = false;
    const int ox = (termW_ - gridW_) / 2, oy = (termH_ - gridH_) / 2;

    // This frame's cells, with the pictures folded into octants and the overlay text snapped onto them.
    const CellGrid folded = terminalFrame(grid);
    const std::vector<Cell>& frame = folded.cells();

    std::string o = "\x1b[?2026h";  // synchronized update: the terminal shows the frame all at once
    const bool full = prev_.size() != frame.size() || prevBg_ != grid.background() || prevOx_ != ox || prevOy_ != oy;
    if (full) {
      appendBg(o, grid.background());
      o += "\x1b[2J";  // margins (and the whole screen) in the background color
      prev_.assign(frame.size(), Cell{U'\0'});
    }
    int curX = -1, curY = -1;
    Rgb fg{}, bg{};
    bool haveFg = false, haveBg = false;
    for (int y = 0; y < gridH_; ++y)
      for (int x = 0; x < gridW_; ++x) {
        const size_t i = static_cast<size_t>(y) * gridW_ + x;
        const Cell& c = frame[i];
        if (!full && c.ch == prev_[i].ch && c.fg == prev_[i].fg && c.bg == prev_[i].bg) continue;
        if (curX != x || curY != y) o += "\x1b[" + std::to_string(oy + y + 1) + ';' + std::to_string(ox + x + 1) + 'H';
        const bool blank = c.ch == U' ';  // a space shows no ink: skip its fg color change
        if (!haveBg || c.bg != bg) { appendBg(o, c.bg); bg = c.bg; haveBg = true; }
        if (!blank && (!haveFg || c.fg != fg)) { appendFg(o, c.fg); fg = c.fg; haveFg = true; }
        appendUtf8(o, termGlyph(c.ch ? c.ch : U' ', glyphMode_ == GlyphMode::Octant));
        curX = x + 1;
        curY = y;
      }
    o += "\x1b[0m\x1b[?2026l";
    writeAll(o);
    prev_ = frame;
    lastFrame_ = frame;
    lastW_ = gridW_;
    lastH_ = gridH_;
    prevBg_ = grid.background();
    prevOx_ = ox;
    prevOy_ = oy;
  }

  std::uint64_t ticksMs() const override {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count());
  }
  bool hasFocus() const override { return focused_; }

  FontInfo fontInfo() const override {
    FontInfo f;
    f.name = "terminal";
    f.cellW = 1;
    f.cellH = 2;
    f.autoFit = false;
    f.windowW = termW_;
    f.windowH = termH_;
    return f;
  }
  void nextFont() override {}  // the terminal's own font is the only one
  void setCellHeight(int) override {}
  int crtLevel() const override { return 0; }
  void setCrtLevel(int) override {}
  int textSixths() const override { return 6; }  // overlay text is one character per cell
  void setTextSixths(int) override {}
  bool isTerminal() const override { return true; }
  bool preferredGrid(int& cols, int& rows) const override {
    cols = std::min(termW_, kMaxCols);
    rows = std::min(termH_, kMaxRows);
    return true;
  }

 private:
  void writeAll(const std::string& s) {
    size_t done = 0;
    while (done < s.size()) {
      const ssize_t n = write(STDOUT_FILENO, s.data() + done, s.size() - done);
      if (n > 0) done += static_cast<size_t>(n);
      else if (n < 0 && errno != EINTR && errno != EAGAIN) return;
    }
  }

  // ---- what this terminal can do (render/TermCaps.hpp) ----------------------------------------------------------------------------

  static std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
  }

  // The output of a fixed tmux command (empty if tmux is not there or says nothing); only used when TMUX is set.
  static std::string tmuxSays(const char* command) {
    std::string out;
    if (FILE* p = popen(command, "r")) {
      char buf[512];
      while (std::fgets(buf, sizeof buf, p) && out.size() < 4096) out += buf;
      pclose(p);
    }
    return out;
  }

  // Reads whatever the terminal sends within `ms`.
  std::string readFor(int ms) {
    std::string got;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
      const int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count());
      if (left <= 0) break;
      pollfd p{STDIN_FILENO, POLLIN, 0};
      if (poll(&p, 1, left) <= 0) break;
      char buf[256];
      const ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
      if (n <= 0) break;
      got.append(buf, static_cast<size_t>(n));
      if (parseTermReplies(got).answered) break;  // the last query is answered: that is all of them
    }
    return got;
  }

  // A centred screen of text lines (each with its width on screen, as some hold escape sequences); `title` is the first line.
  void showScreen(const std::vector<std::pair<std::string, int>>& lines) {
    std::string o = "\x1b[0m\x1b[2J";
    const int top = std::max(1, (termH_ - static_cast<int>(lines.size())) / 2 + 1);
    for (size_t i = 0; i < lines.size(); ++i) {
      const int col = std::max(1, (termW_ - lines[i].second) / 2 + 1);
      o += "\x1b[" + std::to_string(top + static_cast<int>(i)) + ";" + std::to_string(col) + "H" + lines[i].first;
    }
    writeAll(o);
  }
  static std::pair<std::string, int> plain(const std::string& s) { return {s, static_cast<int>(s.size())}; }

  // Waits for a key: 'y' / 'n' (either case), Enter, or Esc (also Ctrl-C), which quits the game here and now.
  char waitKey(const char* accepted) {
    for (;;) {
      pollfd p{STDIN_FILENO, POLLIN, 0};
      if (poll(&p, 1, -1) < 0 && errno != EINTR) continue;
      if (g_quit) leaveAndExit();
      if (!(p.revents & POLLIN)) continue;
      char buf[64];
      const ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
      if (n <= 0) continue;
      if (buf[0] == 0x03 || (buf[0] == 0x1B && n == 1)) leaveAndExit();
      const char c = buf[0] >= 'A' && buf[0] <= 'Z' ? static_cast<char>(buf[0] + 32) : buf[0];
      for (const char* a = accepted; *a; ++a)
        if (c == *a) return c;
    }
  }
  [[noreturn]] void leaveAndExit() {
    restoreTerminal();
    std::exit(0);
  }

  // Finds out what the terminal can do and, where that is in doubt, says so and asks: truecolor, then the octant glyphs.
  void negotiate() {
    TermReplies replies;
    if (env("LABYTERM_NO_QUERY").empty()) {
      writeAll(termQueries());
      replies = parseTermReplies(readFor(500));
    }
    writeAll("\x1b[0m\x1b[2J");
    if (env("TERM") == "linux" && env("LABYTERM_TRUECOLOR").empty()) {  // the kernel's text console: no true color, and its font has none of the block characters
      showScreen({plain("Linux text console"),
                  plain(""),
                  plain("This looks like the bare Linux text console. It cannot show the true colors and block"),
                  plain("characters LabyTerm draws with, so the game will look broken here."),
                  plain(""),
                  plain("Run it from a graphical terminal instead (Ghostty, kitty, foot, WezTerm, Alacritty, ...)."),
                  plain(""),
                  plain("[Enter] Try anyway     [Esc] Quit"),
                  plain(""),
                  plain("(Set LABYTERM_TRUECOLOR=1 to skip this notice.)")});
      waitKey("\r\n");
    }
    const std::string colorterm = env("COLORTERM");
    // tmux swallows the colour query, so it is asked directly what it passes on
    const bool inTmux = !env("TMUX").empty();
    const bool tmuxRgb = inTmux && (tmuxTextHasRgb(tmuxSays("tmux display-message -p '#{client_termfeatures}' 2>/dev/null")) ||
                                    tmuxTextHasRgb(tmuxSays("tmux show -sv terminal-overrides 2>/dev/null")));
    if (env("LABYTERM_TRUECOLOR").empty() && !tmuxRgb && !truecolorLikely(colorterm, replies)) {
      showScreen({plain("True color not detected"),
                  plain(""),
                  plain("LabyTerm draws with 24-bit colors, and this terminal did not report that it has them"),
                  plain(colorterm.empty() ? "(COLORTERM is not set)." : "(COLORTERM is '" + colorterm + "')."),
                  plain(""),
                  plain("If this bar is a smooth gradient, true color works and you can carry on:"),
                  {colorSwatch(60), 60},
                  plain(""),
                  plain("If it shows steps or odd colors, use a true-color terminal (Ghostty, kitty, foot, WezTerm,"),
                  plain("Alacritty, ...). Over ssh, make sure COLORTERM=truecolor reaches the game."),
                  plain(inTmux ? "In tmux (3.2 or later), add this line to ~/.tmux.conf and restart tmux:  set -as terminal-features ',*:RGB'" : ""),
                  plain(""),
                  plain("[Enter] Continue anyway     [Esc] Quit"),
                  plain(""),
                  plain("(Set LABYTERM_TRUECOLOR=1 to skip this check.)")});
      waitKey("\r\n");
    }
    const std::string id = terminalId(replies, env("TERM_PROGRAM"), env("TERM"));
    const std::string want = env("LABYTERM_GLYPHS");
    const std::string dir = stateDir();
    if (want == "octant") glyphMode_ = GlyphMode::Octant;
    else if (want == "quadrant") glyphMode_ = GlyphMode::Quadrant;
    else if (want != "ask" && knownOctantTerminal(id)) glyphMode_ = GlyphMode::Octant;
    else if (const auto saved = want == "ask" ? std::nullopt : loadGlyphChoice(dir, id)) glyphMode_ = *saved;
    else glyphMode_ = calibrate(id, dir);
    writeAll("\x1b[0m\x1b[2J");
  }

  // Shows both kinds of picture and asks which one looks right; the answer is kept for this terminal.
  GlyphMode calibrate(const std::string& id, const std::string& dir) {
    constexpr int kCols = 22, kRows = 6;
    std::vector<std::pair<std::string, int>> lines = {
        plain("Terminal check"),
        plain(""),
        plain("LabyTerm draws its pictures with small block characters (Unicode 16 \"octants\")."),
        plain("This terminal: " + id),
        plain(""),
        plain(std::string("Octants (sharper)").append(kCols - 17, ' ') + std::string(20, ' ') + "Quadrants (simple fallback)"),
    };
    for (int r = 0; r < kRows; ++r) lines.push_back({sampleRing(GlyphMode::Octant, kCols, kRows, r) + "      " + std::string(14, ' ') + sampleRing(GlyphMode::Quadrant, kCols, kRows, r), 2 * kCols + 20});
    lines.push_back(plain(""));
    lines.push_back(plain("Is the LEFT picture a clean ring with an X, with no boxes, '?' or gaps?"));
    lines.push_back(plain(""));
    lines.push_back(plain("[Y] Yes, it looks right     [N] No, use the simple fallback     [Esc] Quit"));
    lines.push_back(plain(""));
    lines.push_back(plain("(Remembered for this terminal. LABYTERM_GLYPHS=ask asks again.)"));
    showScreen(lines);
    const GlyphMode mode = waitKey("yn") == 'y' ? GlyphMode::Octant : GlyphMode::Quadrant;
    saveGlyphChoice(dir, id, mode);
    return mode;
  }

  // After the alternate screen is left, writes the last frame of the game into the normal screen, in its colours, so it stays in the
  // terminal's scrollback and the shell prompt lands right under it: the player can see they were playing in text.
  void leaveFinalScreen() {
    if (lastFrame_.empty() || lastW_ <= 0 || lastH_ <= 0 || lastFrame_.size() != static_cast<size_t>(lastW_) * static_cast<size_t>(lastH_)) return;
    std::string o;
    for (int y = 0; y < lastH_; ++y) {
      Rgb fg{}, bg{};
      bool haveFg = false, haveBg = false;
      for (int x = 0; x < lastW_; ++x) {
        const Cell& c = lastFrame_[static_cast<size_t>(y) * lastW_ + x];
        const bool blank = c.ch == U' ' || c.ch == U'\0';
        if (!haveBg || c.bg != bg) { appendBg(o, c.bg); bg = c.bg; haveBg = true; }
        if (!blank && (!haveFg || c.fg != fg)) { appendFg(o, c.fg); fg = c.fg; haveFg = true; }
        appendUtf8(o, termGlyph(blank ? U' ' : c.ch, glyphMode_ == GlyphMode::Octant));
      }
      o += "\x1b[0m\r\n";
    }
    writeAll(o);
  }

  void measure() {
    g_winch = 0;
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
      termW_ = ws.ws_col;
      termH_ = ws.ws_row;
    }
    prev_.clear();  // the screen content is unknown after a resize: redraw everything
    frameWanted_ = true;
  }

  void showTooSmall() {
    if (tooSmallShown_ && !frameWanted_) return;
    tooSmallShown_ = true;
    prev_.clear();
    lastFrame_.clear();
    const int needW = gridW_, needH = gridH_;  // (the app never makes the grid smaller than the minimum)
    const auto plural = [](int n, const char* one) { return std::to_string(n) + " " + one + (n == 1 ? "" : "s"); };
    std::string shortBy;
    if (termW_ < needW) shortBy += plural(needW - termW_, "column");
    if (termH_ < needH) shortBy += (shortBy.empty() ? "" : " and ") + plural(needH - termH_, "row");
    const std::vector<std::string> lines = {
        "LabyTerm needs a terminal of at least " + std::to_string(needW) + " x " + std::to_string(needH) + " cells.",
        "This one is " + std::to_string(termW_) + " x " + std::to_string(termH_) + " (" + shortBy + " short).",
        "",
        "Zoom out with Ctrl+minus (Ctrl+-): it works instantly.",
        "A few presses is usually enough.",
        "(Some terminals use Ctrl+Shift+minus.) Or enlarge the window.",
        "The game continues by itself once the window is big enough.",
    };
    std::string o = "\x1b[0m\x1b[2J";
    const int top = std::max(1, (termH_ - static_cast<int>(lines.size())) / 2 + 1);
    for (size_t i = 0; i < lines.size(); ++i) {
      std::string line = lines[i];
      if (static_cast<int>(line.size()) > termW_) line.resize(static_cast<size_t>(termW_));  // (ASCII only, so bytes are columns)
      const int col = std::max(1, (termW_ - static_cast<int>(line.size())) / 2 + 1);
      o += "\x1b[" + std::to_string(top + static_cast<int>(i)) + ";" + std::to_string(col) + "H" + line;
    }
    writeAll(o);
  }

  // Fills the event queue from the terminal; true with the next event, or false on timeout.
  bool next(Event& out, int timeoutMs) {
    for (;;) {
      if (g_quit) {
        g_quit = 0;
        out = Event{};
        out.type = Event::Type::Quit;
        return true;
      }
      if (!queue_.empty()) {
        out = queue_.front();
        queue_.pop_front();
        return true;
      }
      if (g_winch) {
        measure();
        return false;  // needsFrame() is now true
      }
      pollfd p{STDIN_FILENO, POLLIN, 0};
      const int r = poll(&p, 1, timeoutMs);
      if (r < 0 && errno == EINTR) continue;  // a resize or quit signal: handled at the top of the loop
      if (r <= 0) return false;
      readInput();
      if (queue_.empty()) {
        if (timeoutMs == 0) return false;
        // the bytes were only part of something (a lone escape prefix); wait again within the same budget
        timeoutMs = std::max(0, timeoutMs < 0 ? timeoutMs : timeoutMs - 1);
      }
    }
  }

  void push(Event::Type t, Key k = Key::None, char32_t text = 0, bool focus = false) {
    Event e;
    e.type = t;
    e.key = k;
    e.text = text;
    e.focused = focus;
    queue_.push_back(e);
  }

  void readInput() {
    char buf[256];
    const ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
    if (n <= 0) return;
    pending_.append(buf, static_cast<size_t>(n));
    parse();
  }

  // Turns the buffered bytes into events. A lone ESC is the Escape key, but may also start a sequence whose rest has
  // not arrived yet (rare on a local terminal): wait a few ms for it.
  void parse() {
    size_t i = 0;
    while (i < pending_.size()) {
      const unsigned char c = static_cast<unsigned char>(pending_[i]);
      if (c == 0x1B) {
        if (i + 1 >= pending_.size()) {
          pollfd p{STDIN_FILENO, POLLIN, 0};
          if (poll(&p, 1, escWaitMs_) > 0) {
            char more[64];
            const ssize_t m = read(STDIN_FILENO, more, sizeof more);
            if (m > 0) {
              pending_.append(more, static_cast<size_t>(m));
              continue;
            }
          }
          push(Event::Type::KeyDown, Key::Escape);
          ++i;
          continue;
        }
        const char intro = pending_[i + 1];
        if (intro == '[' || intro == 'O') {
          size_t j = i + 2;
          while (j < pending_.size() && !(static_cast<unsigned char>(pending_[j]) >= 0x40 && static_cast<unsigned char>(pending_[j]) <= 0x7E)) ++j;
          if (j >= pending_.size()) break;  // incomplete sequence: keep it for the next read
          switch (pending_[j]) {
            case 'A': push(Event::Type::KeyDown, Key::Up); break;
            case 'B': push(Event::Type::KeyDown, Key::Down); break;
            case 'C': push(Event::Type::KeyDown, Key::Right); break;
            case 'D': push(Event::Type::KeyDown, Key::Left); break;
            case 'I': focused_ = true; push(Event::Type::Focus, Key::None, 0, true); frameWanted_ = true; break;
            case 'O': if (intro == '[') { focused_ = false; push(Event::Type::Focus, Key::None, 0, false); } break;
            default: break;  // other keys (Home, Delete, function keys, ...) are not used
          }
          i = j + 1;
          continue;
        }
        push(Event::Type::KeyDown, Key::Escape);  // Alt+key: treat the ESC alone
        ++i;
        continue;
      }
      ++i;
      if (c == 0x03 || c == 0x04) push(Event::Type::Quit);       // Ctrl-C / Ctrl-D
      else if (c == '\r' || c == '\n') push(Event::Type::KeyDown, Key::Enter);
      else if (c == 0x7F || c == 0x08) push(Event::Type::KeyDown, Key::Backspace);
      else if (c >= 0x20 && c < 0x7F) {
        const char lower = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        push(Event::Type::KeyDown, keyForChar(lower), static_cast<char32_t>(lower));
        push(Event::Type::Text, Key::None, c);
      }
      // other bytes (UTF-8 continuation, control codes) are dropped
    }
    pending_.erase(0, i);
  }

  int gridW_, gridH_;
  int termW_ = 80, termH_ = 24;
  std::chrono::steady_clock::time_point start_;
  std::deque<Event> queue_;
  std::string pending_;
  bool frameWanted_ = true, focused_ = true, tooSmallShown_ = false;
  // How long a lone ESC waits for the rest of an arrow key's bytes: a few ms on a local terminal; over a slow link they can arrive further
  // apart, so such sessions can set LABYTERM_ESC_MS.
  static int escWaitFromEnv() {
    const char* v = std::getenv("LABYTERM_ESC_MS");
    const int ms = v ? std::atoi(v) : 0;
    return ms >= 5 && ms <= 1000 ? ms : 25;
  }
  int escWaitMs_ = escWaitFromEnv();
  GlyphMode glyphMode_ = GlyphMode::Octant;
  std::vector<Cell> prev_, lastFrame_;  // prev_: what the screen shows; lastFrame_: the last complete frame, kept for the exit
  int lastW_ = 0, lastH_ = 0;
  Rgb prevBg_{};
  int prevOx_ = -1, prevOy_ = -1;
};

}  // namespace

std::unique_ptr<Backend> makeTerminalBackend(int gridW, int gridH) { return std::make_unique<TerminalBackend>(gridW, gridH); }

}  // namespace laby
