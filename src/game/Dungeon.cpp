#include "game/Dungeon.hpp"

#include "core/Tuning.hpp"

namespace laby {

Dungeon::Dungeon(int height, int width)
    : height_(height), width_(width), level_(height, width), base_(height, width), seen_(static_cast<size_t>(height) * static_cast<size_t>(width), false) {}

namespace {
// The widest open space allowed: how far you can see (whole cells) plus the knob's extra.
int openSpan(const Tuning::Dungeon& t) { return static_cast<int>(tuning().view.drawDistance) + t.openSpanExtra; }
}  // namespace

LevelParams Dungeon::firstFloorParams(const Tuning::Dungeon& t) {
  return LevelParams{t.maxRooms, t.startRoomSize, t.startScattering, openSpan(t), t.extraLoops};
}

LevelParams Dungeon::paramsForFloor(const Tuning::Dungeon& t, int floor) {
  return LevelParams{t.maxRooms, t.roomSizeMin + floor % t.roomSizeCycle, t.scatteringMin + floor % t.scatteringCycle, openSpan(t), t.extraLoops};
}

void Dungeon::rebuild(int floor, const LevelParams& p, Rng& rng, LevelGenState& gen) {
  floor_ = floor;
  level_ = Level(height_, width_);
  level_.setParams(p);
  level_.generate(rng, gen);
  wipeMiniMap();  // a new Dungeon starts with nothing seen
}

XY Dungeon::beginNewGame(Rng& rng, LevelGenState& gen, const Tuning::Dungeon& t, const TreasureLooted& looted) {
  rebuild(1, firstFloorParams(t), rng, gen);
  const XY start = initPlayer();
  initBoss();
  initLevelTreasure(looted);
  snapshotBase();
  return start;
}

XY Dungeon::beginFloor(int floor, Rng& rng, LevelGenState& gen, const Tuning::Dungeon& t, const TreasureLooted& looted) {
  rebuild(floor, paramsForFloor(t, floor), rng, gen);
  const XY elevator{level_.elevatorX(), level_.elevatorY()};
  initBoss();  // far from the player (end of the room tree)
  initLevelTreasure(looted);
  setTileType(elevator.x, elevator.y, TileClass::ELEVATOR);
  markTileSeen(elevator.x, elevator.y);
  snapshotBase();
  return elevator;
}

XY Dungeon::initPlayer() const {
  const Pos p = level_.floorNear(level_.room(level_.rootRoomId()).center);
  return XY{p.c, p.r};
}

void Dungeon::initBoss() {
  const Pos p = level_.floorNear(level_.room(level_.endRoomId()).center);
  level_.tile(p.r, p.c).cls = TileClass::BOSS;
}

void Dungeon::initLevelTreasure(const TreasureLooted& looted) {
  if (looted && looted(floor_)) return;  // already looted on an earlier visit: no treasure this time
  const Pos p = level_.floorNear(level_.room(level_.treasureRoomId()).center);
  level_.tile(p.r, p.c).cls = TileClass::TREASURE;
}

TileClass Dungeon::peekTileType(int x, int y) const {
  if (x > width_ || x < 1 || y > height_ || y < 1) return TileClass::WALL;
  TileClass t = level_.tile(y, x).cls;
  if (t == TileClass::SOIL || t == TileClass::VEIN) t = TileClass::WALL;
  return t;
}

TileClass Dungeon::getTileType(int x, int y) {
  const TileClass t = peekTileType(x, y);
  markTileSeen(x, y);
  return t;
}

bool Dungeon::isTileSeen(int x, int y) const {
  if (y < 1 || x < 1 || y > height_ || x > width_) return false;
  return seen_[static_cast<size_t>(y - 1) * static_cast<size_t>(width_) + static_cast<size_t>(x - 1)];
}

void Dungeon::markTileSeen(int x, int y) {
  if (y < 1 || x < 1 || y > height_ || x > width_) return;
  seen_[static_cast<size_t>(y - 1) * static_cast<size_t>(width_) + static_cast<size_t>(x - 1)] = true;
}

FloorMemory Dungeon::remember(int px, int py, TileClass under) const {
  FloorMemory m;
  m.seen = seen_;
  for (int y = 1; y <= height_; ++y)
    for (int x = 1; x <= width_; ++x)
    {
      TileClass cls = level_.tile(y, x).cls;
      if (cls == TileClass::PLAYER && x == px && y == py) cls = under;
      if (cls != base_.tile(y, x).cls) m.tiles.push_back({x, y, static_cast<int>(cls)});
    }
  m.secretOpen = level_.secretOpen;
  m.secretRevealed = level_.secretRevealed;
  return m;
}

void Dungeon::recall(const FloorMemory& m) {
  if (m.seen.size() == seen_.size()) seen_ = m.seen;
  for (const auto& t : m.tiles)
    if (inGrid(t[0], t[1])) setTileType(t[0], t[1], static_cast<TileClass>(t[2]));
  level_.secretOpen = m.secretOpen;
  level_.secretRevealed = m.secretRevealed;
}

void Dungeon::wipeMiniMap() { seen_.assign(seen_.size(), false); }

void Dungeon::revealStartingTiles() {
  const int ex = level_.elevatorX(), ey = level_.elevatorY();
  for (int i = -1; i <= 1; ++i)
    for (int j = -1; j <= 1; ++j) markTileSeen(ex + i, ey + j);
}

}  // namespace laby
