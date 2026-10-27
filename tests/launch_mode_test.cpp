// modes.json: the start screen's hint about the other way of playing goes away once both ways have been started.
#include <cstdio>
#include <filesystem>

#include "game/Persist.hpp"

namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

int main() {
  const std::string dir = (fs::temp_directory_path() / "labyterm_launch_mode_test").string();
  fs::remove_all(dir);
  check(!laby::recordLaunchMode(dir, true), "the first start (terminal) has not used both");
  check(!laby::recordLaunchMode(dir, true), "the terminal again still has not");
  check(laby::recordLaunchMode(dir, false), "then the window: both used");
  check(laby::recordLaunchMode(dir, true), "and it stays so");
  check(!laby::recordLaunchMode("", true), "no folder: nothing is recorded");
  { FILE* f = std::fopen((fs::path(dir) / "modes.json").c_str(), "w"); std::fputs("not json", f); std::fclose(f); }
  check(!laby::recordLaunchMode(dir, false), "an unreadable file starts over");
  fs::remove_all(dir);
  if (!failures) std::printf("launch_mode_test: ok\n");
  return failures ? 1 : 0;
}
