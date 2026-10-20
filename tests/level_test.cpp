// Checks the level generator over many seeds and floors: every floor is connected, no open space is wider
// than the draw distance + 1, and the elevator, boss, treasure, doors and secret door are where they
// should be. The span check here is written independently of the generator's own.
//   level_test [seeds] [floors]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

#include "core/Rng.hpp"
#include "core/Tuning.hpp"
#include "game/Dungeon.hpp"

using namespace laby;

static int failures = 0;
#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) {                                        \
      if (++failures <= 20) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } \
    }                                                     \
  } while (0)

static bool open(const Level& l, int r, int c) {
  if (r < 0 || r > l.height() + 1 || c < 0 || c > l.width() + 1) return false;
  const TileClass t = l.tile(r, c).cls;
  return t == TileClass::FLOOR || t == TileClass::ELEVATOR || t == TileClass::BOSS || t == TileClass::TREASURE ||
         t == TileClass::O_DOOR || t == TileClass::C_DOOR;
}

// Widest/tallest mass of tiles that sit in a 2x2 open block (see Level::spansWithinLimit).
static int widestMass(const Level& l) {
  const int H = l.height() + 2, W = l.width() + 2;
  std::vector<char> thick(static_cast<size_t>(H * W), 0), seen(thick.size(), 0);
  const auto at = [&](int r, int c) -> size_t { return static_cast<size_t>(r * W + c); };
  for (int r = 0; r + 1 < H; ++r)
    for (int c = 0; c + 1 < W; ++c)
      if (open(l, r, c) && open(l, r + 1, c) && open(l, r, c + 1) && open(l, r + 1, c + 1))
        thick[at(r, c)] = thick[at(r + 1, c)] = thick[at(r, c + 1)] = thick[at(r + 1, c + 1)] = 1;
  int widest = 0;
  for (int r = 0; r < H; ++r)
    for (int c = 0; c < W; ++c) {
      if (!thick[at(r, c)] || seen[at(r, c)]) continue;
      int r0 = r, r1 = r, c0 = c, c1 = c;
      std::deque<std::pair<int, int>> q{{r, c}};
      seen[at(r, c)] = 1;
      while (!q.empty()) {
        auto [pr, pc] = q.front();
        q.pop_front();
        r0 = std::min(r0, pr); r1 = std::max(r1, pr); c0 = std::min(c0, pc); c1 = std::max(c1, pc);
        for (int dr = -1; dr <= 1; ++dr)
          for (int dc = -1; dc <= 1; ++dc) {
            const int nr = pr + dr, nc = pc + dc;
            if (nr < 0 || nr >= H || nc < 0 || nc >= W || !thick[at(nr, nc)] || seen[at(nr, nc)]) continue;
            seen[at(nr, nc)] = 1;
            q.push_back({nr, nc});
          }
      }
      widest = std::max({widest, r1 - r0 + 1, c1 - c0 + 1});
    }
  return widest;
}

int main(int argc, char** argv) {
  const int seeds = argc > 1 ? std::atoi(argv[1]) : 300;
  const int floors = argc > 2 ? std::atoi(argv[2]) : 12;
  const Tuning::Dungeon& t = tuning().dungeon;
  const int span = static_cast<int>(tuning().view.drawDistance) + t.openSpanExtra;
  int widestSeen = 0;
  long roomsTotal = 0, levels = 0, doorsTotal = 0;

  for (int seed = 1; seed <= seeds; ++seed) {
    Rng rng(static_cast<std::uint64_t>(seed));
    LevelGenState gen;
    Dungeon d(t.sizeX, t.sizeY);
    for (int f = 1; f <= floors; ++f) {
      if (f == 1) d.beginNewGame(rng, gen, t, nullptr);
      else d.beginFloor(f, rng, gen, t, nullptr);
      const Level& l = d.level();
      ++levels;
      roomsTotal += static_cast<long>(l.rooms().size());
      doorsTotal += static_cast<long>(l.entrances().size());
      const int where = seed * 1000 + f;

      CHECK(l.rooms().size() >= 3, "[%d] only %zu rooms", where, l.rooms().size());
      const int w = widestMass(l);
      widestSeen = std::max(widestSeen, w);
      CHECK(w <= span, "[%d] open space %d wide (limit %d)", where, w, span);

      // everything the player needs must be reachable from the elevator without walls (doors count as open)
      std::vector<char> seen(static_cast<size_t>((l.height() + 2) * (l.width() + 2)), 0);
      const auto at = [&](int r, int c) -> size_t { return static_cast<size_t>(r * (l.width() + 2) + c); };
      std::deque<std::pair<int, int>> q{{l.elevatorY(), l.elevatorX()}};
      seen[at(l.elevatorY(), l.elevatorX())] = 1;
      int boss = 0, treasure = 0;
      while (!q.empty()) {
        auto [r, c] = q.front();
        q.pop_front();
        if (l.tile(r, c).cls == TileClass::BOSS) ++boss;
        if (l.tile(r, c).cls == TileClass::TREASURE) ++treasure;
        for (auto [dr, dc] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
          const int nr = r + dr, nc = c + dc;
          if (!open(l, nr, nc) || seen[at(nr, nc)]) continue;
          seen[at(nr, nc)] = 1;
          q.push_back({nr, nc});
        }
      }
      CHECK(boss == 1, "[%d] boss reachable %d times", where, boss);
      CHECK(treasure == 1, "[%d] treasure reachable %d times", where, treasure);
      for (const Room& room : l.rooms()) CHECK(seen[at(room.center.r, room.center.c)], "[%d] room %d unreachable", where, room.id);

      CHECK(l.endRoomId() != l.rootRoomId() && l.treasureRoomId() != l.rootRoomId() && l.treasureRoomId() != l.endRoomId(),
            "[%d] start/boss/treasure rooms overlap", where);
      CHECK(l.tile(l.elevatorY(), l.elevatorX()).roomId == l.rootRoomId(), "[%d] elevator outside the start room", where);
      CHECK(l.hasSecretDoor() && l.tile(l.secretY(), l.secretX()).cls == TileClass::SECRET, "[%d] no secret door", where);

      // nothing open on the outer ring; every open tile is enclosed by wall or open tiles
      for (int r = 0; r <= l.height() + 1; ++r)
        for (int c = 0; c <= l.width() + 1; ++c) {
          const bool edge = r == 0 || c == 0 || r == l.height() + 1 || c == l.width() + 1;
          if (edge) CHECK(!open(l, r, c), "[%d] open tile on the border at %d,%d", where, r, c);
          if (!open(l, r, c)) continue;
          for (int dr = -1; dr <= 1; ++dr)
            for (int dc = -1; dc <= 1; ++dc)
              CHECK(l.tile(r + dr, c + dc).cls != TileClass::EMPTY, "[%d] open tile %d,%d borders rock", where, r, c);
        }
    }
  }
  {  // the same seed and state give the same floor
    const auto make = [&](int seed) {
      Rng rng(static_cast<std::uint64_t>(seed));
      LevelGenState gen;
      Dungeon d(t.sizeX, t.sizeY);
      d.beginFloor(4, rng, gen, t, nullptr);
      std::string out;
      for (int r = 0; r <= d.level().height() + 1; ++r)
        for (int c = 0; c <= d.level().width() + 1; ++c) out += static_cast<char>(d.level().tile(r, c).cls);
      return out;
    };
    CHECK(make(77) == make(77), "same seed gave different floors");
    CHECK(make(77) != make(78), "different seeds gave the same floor");
  }
  std::printf("%ld levels: widest open space %d (limit %d), %.1f rooms and %.1f doorway candidates per floor on average\n", levels,
              widestSeen, span, static_cast<double>(roomsTotal) / static_cast<double>(levels), static_cast<double>(doorsTotal) / static_cast<double>(levels));
  if (failures) { std::printf("%d failures\n", failures); return 1; }
  return 0;
}
