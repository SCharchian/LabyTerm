#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "render/CellGrid.hpp"

namespace laby {

enum class Key {
  None, Up, Down, Left, Right,
  Enter, Escape, A, B,  // A/B: Playdate buttons (Z/X); Z confirms like Enter
  Backspace, Other
};

struct Event {
  enum class Type { None, Quit, KeyDown, KeyUp, Text, Focus } type = Type::None;
  Key key = Key::None;
  // Type::Text: the typed character (name entry etc.).
  // Type::KeyDown: the lowercase ASCII letter/digit/punctuation of the key (0 if none), for hotkeys.
  char32_t text = 0;
  bool repeat = false;  // Type::KeyDown: an auto-repeat of a held key
  bool focused = false; // Type::Focus: the window gained (true) or lost (false) focus, or was hidden/minimized
};

struct FontInfo {
  std::string name;      // text font in use ("(none)" = synthesized glyphs only)
  int fontIndex = 0, fontCount = 0;
  int cellW = 0, cellH = 0;  // pixels per cell
  bool autoFit = true;       // cell size follows the window size
  int windowW = 0, windowH = 0;  // window size in real pixels
  float displayScale = 1.0f;     // compositor scale factor (1.5 = 150%)
  int crtLevel = 0;              // current CRT filter level
  int textSixths = 6;            // text size in sixths of the cell size (6 = 1x, 9 = 1.5x, 11 = 1.83x, 12 = 2x)
};

// Abstract presentation + input backend. Implementation: SdlBackend (SDL3 window, our own atlas and frame renderer).
// A terminal (ANSI/notcurses) backend can be added later without touching game code.
class Backend {
 public:
  virtual ~Backend() = default;
  virtual bool pollEvent(Event& out) = 0;
  // Sleeps until an event arrives or `timeoutMs` passes (negative = until an event). Returns true with the event,
  // false on timeout (or when the wake-up was a window event that needs no handling by the app: check needsFrame()).
  // This is what keeps an idle game at ~0% CPU: loops should block here instead of spinning on pollEvent().
  virtual bool waitEvent(Event& out, int timeoutMs) = 0;
  // True when the window needs a new frame even though the game did not change anything (it was resized, exposed,
  // ...). Cleared by present().
  virtual bool needsFrame() const = 0;
  virtual void present(const CellGrid& grid) = 0;
  virtual std::uint64_t ticksMs() const = 0;
  // Whether the window has input focus right now. A window that opens hidden or in the background (a plugin launcher's
  // special workspace) never gets a Focus event, so the app asks once at startup instead of assuming it is focused.
  virtual bool hasFocus() const = 0;

  // Text font and cell size. Cells are always 1:2 (width = height / 2). The glyph atlas is rebuilt
  // at the integer cell size that fits the window, so text and block graphics stay pixel-sharp.
  virtual FontInfo fontInfo() const = 0;
  virtual void nextFont() = 0;             // cycle through the system fonts, then "(none)"
  virtual void setCellHeight(int px) = 0;  // 0 = auto-fit to the window

  // The old-terminal filter (scanlines, glow, beam spread, vignette): 0 off, 1 subtle, 2 default, 3 heavy.
  virtual int crtLevel() const = 0;
  virtual void setCrtLevel(int level) = 0;

  // Overlay text size as a multiple of the cell size, in sixths: 6 = 1x, 9 = 1.5x, 11 = 1.83x, 12 = 2x
  // (CellGrid::printText). The text pitch is floor(cell * n / 6) pixels, so layouts can count on
  // floor(cols * 6 / n) characters per `cols` cells at every cell size.
  virtual int textSixths() const = 0;
  virtual void setTextSixths(int n) = 0;

  // A terminal backend: the game uses its own layout, which follows the terminal's size (ui/TermLayout.hpp).
  virtual bool isTerminal() const { return false; }
  // The grid size this backend wants now (a terminal: its size in cells, clamped). False = keep the size passed to makeBackend.
  virtual bool preferredGrid(int& cols, int& rows) const { (void)cols; (void)rows; return false; }
};

// Which backend makeBackend creates: the SDL window or the terminal. Chosen once at startup (main.cpp: --window / --terminal).
enum class BackendKind { Window, Terminal };
BackendKind& backendKind();

// Creates the backend named by backendKind(). gridW/gridH is the logical cell grid. The window is resizable and the
// glyph atlas is rebuilt at the largest whole cell size that fits (set LABYTERM_CRT=0..3 to pick the
// filter level, default 2); a terminal's grid follows its size.
std::unique_ptr<Backend> makeBackend(int gridW, int gridH, const char* title);

// The two backends (render/SdlBackend.cpp, render/TerminalBackend.cpp), for makeBackend.
std::unique_ptr<Backend> makeSdlBackend(int gridW, int gridH, const char* title);
std::unique_ptr<Backend> makeTerminalBackend(int gridW, int gridH);

}  // namespace laby
