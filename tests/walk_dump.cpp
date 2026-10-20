// Headless walk: generates a dungeon, applies a string of moves (U forward, D back, L turn left, R turn right) and
// renders the resulting screen to a PNG, optionally after every step.
// (U D L R move/turn, E = Enter, X = die in a fight, T/B/K = jump next to the level chest / boss / secret door, N = 100 ms pass (after A), lowercase letters = hotkeys)
//   walk_dump <seed> <moves> <out.png> [cellH=12] [crt=2]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "png_grid.hpp"

#include "core/DataFile.hpp"
#include "core/Settings.hpp"
#include "render/GlyphAtlas.hpp"
#include "ui/ScreenLayout.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: walk_dump <seed> <moves UDLR...> <out.png> [cellH] [crt]\n");
    return 2;
  }
  WalkSession session(std::strtoull(argv[1], nullptr, 10));
  int torchFrame = 0;
  std::uint64_t clock = 1000;  // N advances it by 100 ms
  for (char c : std::string(argv[2])) {
    if (c == 'N') session.tick(clock += 100);
    else if (c == 'Y') session.debugTorch(++torchFrame);  // flicker frame 1, 2, ... (press again for the next; frames swing opposite ways)
    else if (c == 'K') session.debugGoNextTo(TileClass::SECRET);
    else if (c == 'U') session.handle(Key::Up);
    else if (c == 'D') session.handle(Key::Down);
    else if (c == 'L') session.handle(Key::Left);
    else if (c == 'R') session.handle(Key::Right);
    else if (c == 'V') session.debugRideElevator();
    else if (c == 'T') session.debugGoNextTo(TileClass::TREASURE);
    else if (c == 'B') session.debugGoNextTo(TileClass::BOSS);
    else if (c == 'P') session.debugExplore(45);  // mark what a 45-step walk from here would see
    else if (c == 'G') session.debugGiveKit();  // all scrolls, gear in every slot
    else if (c == 'J') session.debugPotionChest(true);   // a health-orb chest (then E to open it; WALK_TIME=<ms> is the orb's age)
    else if (c == 'M') session.debugPotionChest(false);  // a mana-orb chest
    else if (c == 'C') session.debugScrollChest(std::getenv("WALK_SCROLL") ? std::atoi(std::getenv("WALK_SCROLL")) : 0);  // a scroll chest (then E; WALK_ART=<1-4> picks the picture)
    else if (c == 'O') session.debugGoToTown();
    else if (c == 'I') session.debugPortal();  // the town portal screen (the boss's reward)
    else if (c == 'Z') session.debugLevelUp();
    else if (c == 'H') {  // the start screen; WALK_TIME=<ms> sets the storm's clock, WALK_STATE=<dir> a state dir (scores for Legends)
      if (std::getenv("WALK_STATE")) session.setStateDir(std::getenv("WALK_STATE"));
      session.startTitle();
    }
    else if (c == 'Q') session.handle(Key::Escape);
    else if (c == 'A') session.tick(0);  // turn the step glide on
    else if (c == 'F') session.debugFight(false, std::getenv("WALK_MONSTER") ? std::getenv("WALK_MONSTER") : "");
    else if (c == 'X') { session.debugHero().hp = 0; session.handle(Key::Other, U'a'); }  // die in the fight (game over screen)
    else if (c == 'W') { session.tick(1000); session.tick(2000); session.tick(4000); }  // let the fight camera settle
    else if (c == '\n' || c == 'E') session.handle(Key::Enter);
    else if (c >= 'a' && c <= 'z') session.handle(Key::Other, static_cast<char32_t>(c));
  }
  if (const char* a = std::getenv("WALK_ART")) session.debugScrollArt(std::atoi(a) - 1);
  if (const char* t = std::getenv("WALK_TIME")) session.tick(static_cast<std::uint64_t>(std::atoll(t)));
  if (const char* b = std::getenv("WALK_BREATH")) session.debugTorchBreath(std::atof(b));  // seconds into the torch's breathing cycle
  if (const char* m = std::getenv("WALK_MAZE")) settings().mazeStyle = std::string(m) == "pixel" ? 0 : 1;  // WALK_MAZE=pixel: the pixel art (octant mosaic) instead of the default Text maze style
  const int cellH = (argc > 4 ? std::atoi(argv[4]) : 12) & ~1, crt = argc > 5 ? std::atoi(argv[5]) : 2;
  const auto fonts = findFonts(dataDir() + "/fonts");
  std::vector<FontFace> faces(fonts.size());
  std::vector<const FontFace*> chain;
  for (size_t i = 0; i < fonts.size(); ++i)
    if (faces[i].load(fonts[i].path)) chain.push_back(&faces[i]);
  const Atlas atlas = buildGlyphAtlas(cellH / 2, cellH, chain, 11);
  CellGrid grid(kGridCols, kGridRows);
  session.draw(grid, loadTheme(), std::getenv("WALK_LABEL") ? std::getenv("WALK_LABEL") : "walk_dump | seed " + std::string(argv[1]) + " | moves " + std::string(argv[2]));  // WALK_LABEL="seed 7": the corner text, for clean screenshots
  writeGridPng(argv[3], grid, atlas, crt);
  std::printf("%s: player at (%d, %d) facing %d\n", argv[3], session.player().x, session.player().y, static_cast<int>(session.player().facing));
}
