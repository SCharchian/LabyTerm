// Chooses which Latin-1 symbols to add to the Text maze style's character set: draws many dungeon views, collects the cell
// patterns they produce, then adds, one at a time, the candidate that lowers the matching error the most (greedy).
//   text_charset_pick [seeds=12] [max=25] [all]
// Prints each pick with its gain as a share of the current total error; paste the picks into kLatin1Symbols in TextShapes.cpp.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "core/Settings.hpp"
#include "platform/Theme.hpp"
#include "render/TextShapes.hpp"
#include "ui/ScreenLayout.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;

static std::map<std::uint32_t, long> gSeen;
static void observe(std::uint32_t p) { if (p) ++gSeen[p]; }

int main(int argc, char** argv) {
  const int seeds = argc > 1 ? std::atoi(argv[1]) : 12, maxPicks = argc > 2 ? std::atoi(argv[2]) : 25;
  settings().mazeStyle = 1;
  setTextPatternObserver(observe);
  const Theme theme = loadTheme();
  const char* routes[] = {"UURUUULUUURUU", "RUUUUDLLUUUR", "UUUUUUUUUU", "LUURRUUULUUU", "TUUD", "BUU"};
  for (int seed = 1; seed <= seeds; ++seed)
    for (const char* route : routes) {
      WalkSession session(static_cast<std::uint64_t>(seed));
      CellGrid grid(kGridCols, kGridRows);
      for (const char* c = route; *c; ++c) {
        if (*c == 'T') session.debugGoNextTo(TileClass::TREASURE);
        else if (*c == 'B') session.debugGoNextTo(TileClass::BOSS);
        else session.handle(*c == 'U' ? Key::Up : *c == 'D' ? Key::Down : *c == 'L' ? Key::Left : Key::Right);
        session.draw(grid, theme, "pick");
      }
    }
  setTextPatternObserver(nullptr);
  long cells = 0;
  for (const auto& [p, n] : gSeen) cells += n;
  std::printf("%zu distinct patterns in %ld cells\n", gSeen.size(), cells);

  const std::u32string base = U".,:;'\"`-_=+*~^!|/\\()[]{}<>#%&@$?oOxXIli";
  // Candidates: by default only the thin, structural symbols. The busy ones (the copyright-style signs, the fractions, the
  // superscripts) win on error alone, by filling in mid-density cells, but they read as letters and break up flat walls.
  std::u32string cand = U"\xA1\xA6\xA8\xAB\xAC\xAF\xB0\xB1\xB4\xB7\xB8\xBB\xBF\xD7\xF7\xA4";
  if (argc > 3 && std::string(argv[3]) == "all") {
    cand.clear();
    for (char32_t c = 0xA1; c <= 0xBF; ++c)
      if (c != 0xAD) cand += c;
    cand += U"\xD7\xF7";
  }
  auto shapes = measureTextShapes(base);
  const auto extra = measureTextShapes(cand);
  if (shapes.empty() || extra.empty()) { std::fprintf(stderr, "bundled font missing\n"); return 1; }
  std::vector<std::pair<TextSamples, long>> pats;
  for (const auto& [p, n] : gSeen) pats.push_back({softenedPattern(p), n});
  std::vector<float> err(pats.size(), 1e9f);
  const auto refresh = [&](const TextShape& s) {
    for (size_t i = 0; i < pats.size(); ++i) err[i] = std::min(err[i], textShapeError(pats[i].first, s));
  };
  for (const TextShape& s : shapes) refresh(s);
  const auto total = [&] { double t = 0; for (size_t i = 0; i < pats.size(); ++i) t += err[i] * static_cast<double>(pats[i].second); return t; };
  double now = total();
  std::printf("error with ASCII only: %.0f\n", now);
  std::vector<bool> used(extra.size(), false);
  std::u32string picked;
  for (int k = 0; k < maxPicks; ++k) {
    double bestGain = 0;
    int bestI = -1;
    for (size_t c = 0; c < extra.size(); ++c) {
      if (used[c]) continue;
      double gain = 0;
      for (size_t i = 0; i < pats.size(); ++i) gain += std::max(0.0f, err[i] - textShapeError(pats[i].first, extra[c])) * static_cast<double>(pats[i].second);
      if (gain > bestGain) { bestGain = gain; bestI = static_cast<int>(c); }
    }
    if (bestI < 0 || bestGain <= 0) break;
    used[static_cast<size_t>(bestI)] = true;
    refresh(extra[static_cast<size_t>(bestI)]);
    picked += extra[static_cast<size_t>(bestI)].ch;
    now = total();
    std::printf("%2d. U+%04X  gain %5.2f%%   (error now %.0f)\n", k + 1, static_cast<unsigned>(extra[static_cast<size_t>(bestI)].ch), 100 * bestGain / (now + bestGain), now);
  }
  std::printf("\nconst char32_t kLatin1Symbols[] = {");
  for (char32_t c : picked) std::printf("0x%X, ", static_cast<unsigned>(c));
  std::printf("0};\n");
}
