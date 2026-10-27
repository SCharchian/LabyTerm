#pragma once
#include <functional>
#include <string>
#include <vector>

#include "audio/Cues.hpp"
#include "render/Backend.hpp"
#include "ui/MainScreen.hpp"

namespace laby {

// The Esc menu and the pages under it: Options (and Key bindings), Help, About and the quit question. Opened over the game, which waits.
// Back (Esc or X) goes up one level; from the main page it closes the menu.
class SystemMenu {
 public:
  bool open() const { return page_ != Page::None; }
  bool capturing() const { return capture_; }  // waiting for the key to bind: the raw key goes to captureKey()
  bool quitRequested() const { return quit_; }
  void openMain();
  // Opens straight on a page (from the title screen); Esc then closes the menu instead of going up to the main page.
  void openOptions() { openDirect(Page::Options); }
  void openHelp();
  void requestQuit() { quit_ = true; }
  void close() { page_ = Page::None; capture_ = false; }
  // A key already run through the key bindings (see translateKey).
  void key(Key key, char32_t ch);
  // The raw key while capturing.
  void captureKey(Key key, char32_t ch);
  void setAbandon(std::function<void()> f) { abandon_ = std::move(f); }
  // Whether a run is under way: "Abandon run" is greyed out and skipped otherwise.
  void setCanAbandon(std::function<bool()> f) { canAbandon_ = std::move(f); }
  void setSound(std::function<void(Sfx)> f) { sound_ = std::move(f); }

  void build(MenuPanel& out) const;
  // The terminal build: characters per line on a Help page, so the text fills whatever panel the screen has room for (re-wraps when it changes).
  void setPageChars(int n) const {
    if (n != pageChars_) {
      pageChars_ = n;
      helpLoaded_ = false;
    }
  }
  std::string hints() const;

 private:
  enum class Page { None, Main, Options, Keys, Help, Abandon, Quit };
  struct Row { std::string label, value; bool enabled = true; };
  std::vector<Row> optionRows() const;
  void optionChange(int row, int dir, bool enter);
  void loadHelp() const;
  void click() const { if (sound_) sound_(Sfx::CrankClick); }
  void apply() const;
  void openDirect(Page p);
  void leave(Page to, int sel);  // back out of Options / Help / About

  Page page_ = Page::None;
  int sel_ = 0;
  int slot_ = 0;       // Keys: which of an action's two keys is chosen
  bool capture_ = false;
  std::string note_;   // Keys: what happened to the last key bound
  int tab_ = 0, scroll_ = 0;  // Help
  int helpFrom_ = 1;          // Help: the main-page row (Help or About) to return the cursor to
  bool quit_ = false;
  bool direct_ = false;  // opened on a page, not through the main one
  std::function<void(Sfx)> sound_;
  std::function<void()> abandon_;
  std::function<bool()> canAbandon_;
  bool canAbandon() const { return canAbandon_ && canAbandon_(); }
  mutable bool helpLoaded_ = false;
  mutable int pageChars_ = 0;  // 0: the SDL layout's width
  mutable std::vector<std::vector<OverlayLine>> help_;  // by tab
};

}  // namespace laby
