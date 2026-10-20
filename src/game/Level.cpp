#include "game/Level.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <string>

namespace laby {
namespace {

constexpr int kRoomGap = 2;          // wall tiles kept between two rooms' bounding boxes
constexpr int kPlacementTries = 80;  // room placements attempted per layout
constexpr int kNormalAttempts = 30;  // layouts with all shapes and wiggles
constexpr int kPlainAttempts = 10;   // then rectangles and straight corridors only
constexpr int kLastResort = 20;      // then the same without the span check (never expected)

enum Shape { kRect, kChamfer, kPlus, kLShape, kPillars, kShapeCount };

int sign(int v) { return (v > 0) - (v < 0); }

}  // namespace

Level::Level(int height, int width) : height_(height), width_(width) {
  if (height < 10 || width < 10) throw std::runtime_error("Level must have height>=10, width>=10");
  maxRoomSize_ = 5;
  maxRooms_ = static_cast<int>(std::ceil(std::max(height, width) / static_cast<double>(kMinRoomSize)));
  scatteringFactor_ = 3;
  matrix_.assign(static_cast<size_t>(height + 3) * static_cast<size_t>(width + 2), Tile{});
}

void Level::setParams(const LevelParams& p) {
  maxRooms_ = p.maxRooms;
  if (p.maxRoomSize > std::min(height_, width_) || p.maxRoomSize < 3)
    throw std::runtime_error("MaxRoomSize can't be bigger than height/width or smaller than 3");
  maxSpan_ = std::max(kMinRoomSize, p.maxSpan);
  maxRoomSize_ = std::min(p.maxRoomSize, maxSpan_);
  scatteringFactor_ = p.scatteringFactor;
  extraLoops_ = std::max(0, p.extraLoops);
}

Tile& Level::tile(int r, int c) {
  if (r < -1 || r > height_ + 1 || c < 0 || c > width_ + 1)
    throw std::out_of_range("level tile (" + std::to_string(r) + ", " + std::to_string(c) + ") is outside the matrix");
  return matrix_[static_cast<size_t>(r + 1) * static_cast<size_t>(width_ + 2) + static_cast<size_t>(c)];
}
const Tile& Level::tile(int r, int c) const { return const_cast<Level*>(this)->tile(r, c); }

std::array<Pos, 9> Level::adjacent(int r, int c) {
  std::array<Pos, 9> out;
  int i = 0;
  for (int dx = -1; dx <= 1; ++dx)
    for (int dy = -1; dy <= 1; ++dy) out[static_cast<size_t>(i++)] = Pos{r + dy, c + dx};
  return out;
}

Pos Level::floorNear(Pos center) const {
  for (const Pos& p : adjacent(center.r, center.c))
    if (tile(p.r, p.c).cls == TileClass::FLOOR) return p;
  throw std::runtime_error("no floor tile next to (" + std::to_string(center.r) + ", " + std::to_string(center.c) + ")");
}

// ---- generation -------------------------------------------------------------------------------

void Level::generate(Rng& rng, LevelGenState& state) {
  state_ = &state;
  secretSet_ = false;
  secretOpen = secretRevealed = false;

  bool ok = false;
  for (int i = 0; i < kNormalAttempts && !ok; ++i) ok = tryLayout(rng, false, true);
  for (int i = 0; i < kPlainAttempts && !ok; ++i) ok = tryLayout(rng, true, true);
  for (int i = 0; i < kLastResort && !ok; ++i) ok = tryLayout(rng, true, false);
  if (!ok) {
    state_ = nullptr;
    throw std::runtime_error("level generator could not build a connected layout");
  }

  placeElevator(rng);
  addDoors(rng);
  placeSecretDoor(rng);
  state_ = nullptr;
}

void Level::resetMap() {
  std::fill(matrix_.begin(), matrix_.end(), Tile{});
  rooms_.clear();
  entrances_.clear();
  staircases_.clear();
  rootRoom_ = endRoom_ = treasureRoom_ = 0;
}

// One complete attempt. Walls (which draw soil/vein dice and update the carried soil state) are only
// placed once the layout has passed, so a rejected attempt leaves no trace besides the Rng position.
bool Level::tryLayout(Rng& rng, bool plain, bool strict) {
  resetMap();
  placeRooms(rng, plain);
  if (rooms_.size() < 3) return false;  // need a start, a boss room and a treasure room
  buildRoomTree();
  for (const Room& r : std::vector<Room>(rooms_))
    for (int child : r.neighbours)
      if (!carveCorridor(r.id, child, rng, plain)) return false;
  addLoops(rng, plain);
  if (!allRoomsReachable()) return false;
  if (strict && !spansWithinLimit()) return false;
  pickSpecialRooms();
  wallIn(rng);
  return true;
}

void Level::placeRooms(Rng& rng, bool plain) {
  const int maxSide = std::min(maxRoomSize_, std::min(height_ - 2, width_ - 2));
  for (int tries = 0; tries < kPlacementTries && static_cast<int>(rooms_.size()) < maxRooms_; ++tries) {
    const int h = static_cast<int>(rng.range(kMinRoomSize, maxSide));
    const int w = static_cast<int>(rng.range(kMinRoomSize, maxSide));
    const int r0 = static_cast<int>(rng.range(1, height_ - h));  // floor stays on rows 1..height-1, columns 1..width-1
    const int c0 = static_cast<int>(rng.range(1, width_ - w));
    const int roll = static_cast<int>(rng.range(1, 100));
    int shape = kRect;
    if (!plain) {
      if (roll > 45) shape = kChamfer;
      if (roll > 60) shape = kPlus;
      if (roll > 70) shape = kLShape;
      if (roll > 85) shape = kPillars;
    }
    if (shape != kRect && (h < 5 || w < 5)) shape = kRect;

    bool clash = false;
    for (int r = r0 - kRoomGap; r <= r0 + h - 1 + kRoomGap && !clash; ++r)
      for (int c = c0 - kRoomGap; c <= c0 + w - 1 + kRoomGap; ++c)
        if (r >= -1 && r <= height_ + 1 && c >= 0 && c <= width_ + 1 && tile(r, c).roomId != 0) {
          clash = true;
          break;
        }
    if (!clash) buildRoom(r0, c0, h, w, shape, rng);
  }
}

// Lays out one room in the h x w box at (r0, c0). The box center is always floor.
void Level::buildRoom(int r0, int c0, int h, int w, int shape, Rng& rng) {
  Room room;
  room.id = static_cast<int>(rooms_.size()) + 1;
  room.center = Pos{r0 + (h - 1) / 2, c0 + (w - 1) / 2};
  rooms_.push_back(room);

  const int cr = (h - 1) / 2, cc = (w - 1) / 2;  // center, in box coordinates
  const int cornerRows = (h - 1) / 2, cornerCols = (w - 1) / 2;
  const bool top = rng.range(0, 1) == 0, left = rng.range(0, 1) == 0;  // which corner an L loses
  const int cut = (h >= 7 && w >= 7) ? 2 : 1;                           // chamfer size

  for (int r = 0; r < h; ++r)
    for (int c = 0; c < w; ++c) {
      bool floor = true, pillar = false;
      switch (shape) {
        case kChamfer: {
          const int dr = std::min(r, h - 1 - r), dc = std::min(c, w - 1 - c);
          floor = dr + dc >= cut;
          break;
        }
        case kPlus:
          floor = std::abs(r - cr) <= 1 || std::abs(c - cc) <= 1;
          break;
        case kLShape: {
          const bool inRows = top ? r < cornerRows : r >= h - cornerRows;
          const bool inCols = left ? c < cornerCols : c >= w - cornerCols;
          floor = !(inRows && inCols);
          break;
        }
        case kPillars:
          pillar = (r == 1 || r == h - 2) && (c == 1 || c == w - 2);
          floor = !pillar;
          break;
        default:
          break;
      }
      Tile& t = tile(r0 + r, c0 + c);
      t.roomId = room.id;  // pillars belong to the room too (they keep other rooms and corridors off them)
      if (floor) t.cls = TileClass::FLOOR;
    }
}

// Nearest-first spanning tree over the room centers (each step joins the closest unconnected room to
// any connected one). The first room is the start; the boss goes in the room deepest in the tree.
void Level::buildRoomTree() {
  const size_t n = rooms_.size();
  std::vector<bool> inTree(n, false);
  inTree[0] = true;
  for (size_t added = 1; added < n; ++added) {
    double best = std::numeric_limits<double>::infinity();
    size_t from = 0, to = 0;
    for (size_t i = 0; i < n; ++i) {
      if (!inTree[i]) continue;
      for (size_t j = 0; j < n; ++j) {
        if (inTree[j]) continue;
        const double d = rooms_[i].distanceTo(rooms_[j]);
        if (d < best) {
          best = d;
          from = i;
          to = j;
        }
      }
    }
    rooms_[from].neighbours.push_back(rooms_[to].id);
    inTree[to] = true;
  }
  rootRoom_ = rooms_[0].id;
}

// Start = first room. Boss = deepest room in the tree (ties: farthest in a straight line). Treasure =
// the deepest dead end that is neither (any other room if every dead end is taken).
void Level::pickSpecialRooms() {
  const size_t n = rooms_.size();
  std::vector<int> depth(n, 0), childCount(n, 0);
  for (size_t i = 0; i < n; ++i) childCount[i] = static_cast<int>(rooms_[i].neighbours.size());
  std::deque<int> queue{rootRoom_};
  while (!queue.empty()) {
    const int id = queue.front();
    queue.pop_front();
    for (int child : room(id).neighbours) {
      depth[static_cast<size_t>(child - 1)] = depth[static_cast<size_t>(id - 1)] + 1;
      queue.push_back(child);
    }
  }
  const Room& root = room(rootRoom_);
  const auto better = [&](int a, int b) {  // is room a a better "far away" pick than room b?
    const size_t ia = static_cast<size_t>(a - 1), ib = static_cast<size_t>(b - 1);
    if (depth[ia] != depth[ib]) return depth[ia] > depth[ib];
    return room(a).distanceTo(root) > room(b).distanceTo(root);
  };
  endRoom_ = 0;
  for (const Room& r : rooms_)
    if (r.id != rootRoom_ && (endRoom_ == 0 || better(r.id, endRoom_))) endRoom_ = r.id;
  treasureRoom_ = 0;
  for (int pass = 0; pass < 2 && treasureRoom_ == 0; ++pass)
    for (const Room& r : rooms_) {
      if (r.id == rootRoom_ || r.id == endRoom_) continue;
      if (pass == 0 && childCount[static_cast<size_t>(r.id - 1)] != 0) continue;
      if (treasureRoom_ == 0 || better(r.id, treasureRoom_)) treasureRoom_ = r.id;
    }
}

// Carves a one-tile-wide corridor from the middle of one room to the middle of another: straight runs
// along one axis toward the goal, now and then turning early, with occasional sidesteps ("scattering").
// Records the tile just outside the target room as a doorway candidate. Returns false when it gets
// stuck (the layout is then thrown away).
bool Level::carveCorridor(int fromId, int toId, Rng& rng, bool plain) {
  Pos cur = room(fromId).center;
  const Pos goal = room(toId).center;
  int axis = std::abs(goal.c - cur.c) > std::abs(goal.r - cur.r) ? 1 : 0;  // 1 = along columns, 0 = along rows
  const double wiggle = plain ? 0.0 : scatteringFactor_ * 0.05;
  const auto usable = [&](Pos p) {
    if (!withinBounds(p.r, p.c, height_, width_)) return false;
    const Tile& t = tile(p.r, p.c);
    if (t.roomId != 0 && t.cls != TileClass::FLOOR) return false;  // a pillar
    return t.cls == TileClass::FLOOR || !makesFloorBlock(p.r, p.c);
  };

  for (int guard = 0; guard < 8 * (height_ + width_); ++guard) {
    const int dr = goal.r - cur.r, dc = goal.c - cur.c;
    if (dr == 0 && dc == 0) return true;
    if ((axis == 0 && dr == 0) || (axis == 1 && dc == 0) || (dr != 0 && dc != 0 && rng.real() < 0.12)) axis = 1 - axis;

    Pos next = axis == 0 ? Pos{cur.r + sign(dr), cur.c} : Pos{cur.r, cur.c + sign(dc)};
    if (wiggle > 0 && rng.real() < wiggle) {
      const int d = rng.range(0, 1) == 0 ? -1 : 1;  // sidestep across the direction of travel
      const Pos side = axis == 0 ? Pos{cur.r, cur.c + d} : Pos{cur.r + d, cur.c};
      if (usable(side) && tile(side.r, side.c).roomId == 0) next = side;
    }
    if (!usable(next)) {  // blocked: try the other axis, else give up
      next = axis == 0 ? Pos{cur.r, cur.c + sign(dc)} : Pos{cur.r + sign(dr), cur.c};
      if (next == cur || !usable(next)) return false;
    }
    Tile& t = tile(next.r, next.c);
    const bool entersTarget = t.roomId == toId && t.cls == TileClass::FLOOR;
    if (entersTarget && tile(cur.r, cur.c).roomId == 0) entrances_.push_back(cur);
    if (t.cls != TileClass::FLOOR) t.cls = TileClass::FLOOR;
    cur = next;
    if (entersTarget) return true;
  }
  return false;
}

// A few extra corridors between rooms that are close but not yet joined. Failures are harmless: a loop
// that cannot be carved cleanly is simply left out (its partial tiles are undone).
void Level::addLoops(Rng& rng, bool plain) {
  if (plain || extraLoops_ == 0) return;
  const size_t n = rooms_.size();
  std::vector<std::vector<bool>> linked(n, std::vector<bool>(n, false));
  for (const Room& r : rooms_)
    for (int c : r.neighbours) linked[static_cast<size_t>(r.id - 1)][static_cast<size_t>(c - 1)] = linked[static_cast<size_t>(c - 1)][static_cast<size_t>(r.id - 1)] = true;

  for (int k = 0; k < extraLoops_; ++k) {
    std::vector<std::pair<double, std::pair<int, int>>> pairs;
    for (size_t i = 0; i < n; ++i)
      for (size_t j = i + 1; j < n; ++j)
        if (!linked[i][j]) pairs.push_back({rooms_[i].distanceTo(rooms_[j]), {rooms_[i].id, rooms_[j].id}});
    if (pairs.empty()) return;
    std::sort(pairs.begin(), pairs.end());
    const size_t pick = static_cast<size_t>(rng.range(0, static_cast<std::int64_t>(std::min<size_t>(pairs.size(), 3)) - 1));
    const auto [a, b] = pairs[pick].second;

    const std::vector<Tile> backupTiles = matrix_;
    const std::vector<Pos> backupEntrances = entrances_;
    if (carveCorridor(a, b, rng, false)) {
      linked[static_cast<size_t>(a - 1)][static_cast<size_t>(b - 1)] = linked[static_cast<size_t>(b - 1)][static_cast<size_t>(a - 1)] = true;
      if (!spansWithinLimit()) {  // the loop made a wide area: undo it
        matrix_ = backupTiles;
        entrances_ = backupEntrances;
      }
    } else {
      matrix_ = backupTiles;
      entrances_ = backupEntrances;
    }
  }
}

bool Level::makesFloorBlock(int r, int c) const {
  const auto open = [&](int rr, int cc) {
    return rr >= 0 && rr <= height_ + 1 && cc >= 0 && cc <= width_ + 1 && tile(rr, cc).cls == TileClass::FLOOR;
  };
  for (int dr : {-1, 1})
    for (int dc : {-1, 1})
      if (open(r + dr, c) && open(r, c + dc) && open(r + dr, c + dc)) return true;
  return false;
}

bool Level::allRoomsReachable() const {
  std::vector<bool> seen(matrix_.size(), false);
  const auto index = [&](int r, int c) { return static_cast<size_t>(r + 1) * static_cast<size_t>(width_ + 2) + static_cast<size_t>(c); };
  std::deque<Pos> queue{room(rootRoom_).center};
  seen[index(queue.front().r, queue.front().c)] = true;
  while (!queue.empty()) {
    const Pos p = queue.front();
    queue.pop_front();
    for (const Pos d : {Pos{-1, 0}, Pos{1, 0}, Pos{0, -1}, Pos{0, 1}}) {
      const int nr = p.r + d.r, nc = p.c + d.c;
      if (nr < 0 || nr > height_ || nc < 0 || nc > width_ || !isPassable(nr, nc) || seen[index(nr, nc)]) continue;
      seen[index(nr, nc)] = true;
      queue.push_back(Pos{nr, nc});
    }
  }
  for (const Room& r : rooms_)
    if (!seen[index(r.center.r, r.center.c)]) return false;
  return true;
}

// "Open space": every tile that is part of some 2x2 block of floor. Neighbouring such tiles (8-way,
// so pillars do not split a hall) form one mass; no mass may be wider or taller than maxSpan_.
// Corridors are one tile wide and never form blocks, so their length is not limited.
bool Level::spansWithinLimit() const {
  const int cols = width_ + 2;
  const auto idx = [&](int r, int c) { return static_cast<size_t>(r + 1) * static_cast<size_t>(cols) + static_cast<size_t>(c); };
  std::vector<char> thick(matrix_.size(), 0), seen(matrix_.size(), 0);
  for (int r = 0; r < height_; ++r)
    for (int c = 0; c < width_; ++c)
      if (isPassable(r, c) && isPassable(r + 1, c) && isPassable(r, c + 1) && isPassable(r + 1, c + 1))
        thick[idx(r, c)] = thick[idx(r + 1, c)] = thick[idx(r, c + 1)] = thick[idx(r + 1, c + 1)] = 1;
  for (int r = 0; r <= height_; ++r)
    for (int c = 0; c <= width_; ++c) {
      if (!thick[idx(r, c)] || seen[idx(r, c)]) continue;
      int rmin = r, rmax = r, cmin = c, cmax = c;
      std::deque<Pos> queue{Pos{r, c}};
      seen[idx(r, c)] = 1;
      while (!queue.empty()) {
        const Pos p = queue.front();
        queue.pop_front();
        rmin = std::min(rmin, p.r);
        rmax = std::max(rmax, p.r);
        cmin = std::min(cmin, p.c);
        cmax = std::max(cmax, p.c);
        for (int dr = -1; dr <= 1; ++dr)
          for (int dc = -1; dc <= 1; ++dc) {
            const int nr = p.r + dr, nc = p.c + dc;
            if (nr < 0 || nr > height_ || nc < 0 || nc > width_ || !thick[idx(nr, nc)] || seen[idx(nr, nc)]) continue;
            seen[idx(nr, nc)] = 1;
            queue.push_back(Pos{nr, nc});
          }
      }
      if (rmax - rmin + 1 > maxSpan_ || cmax - cmin + 1 > maxSpan_) return false;
    }
  return true;
}

// Rock stays EMPTY; everything touching the open area becomes wall (mostly plain, with soil clusters
// and mineral veins), and so does the outer border.
void Level::wallIn(Rng& rng) {
  for (int r = 0; r <= height_ + 1; ++r)
    for (int c = 0; c <= width_ + 1; ++c) {
      const Tile& t = tile(r, c);
      if (t.cls == TileClass::FLOOR) {
        for (const Pos& p : adjacent(r, c))
          if (p.r >= 0 && p.r <= height_ + 1 && p.c >= 0 && p.c <= width_ + 1 && tile(p.r, p.c).cls == TileClass::EMPTY) placeWall(p.r, p.c, rng);
      }
    }
  for (int c = 0; c <= width_ + 1; ++c) {
    placeWall(0, c, rng);
    placeWall(height_ + 1, c, rng);
  }
  for (int r = 0; r <= height_ + 1; ++r) {
    placeWall(r, 0, rng);
    placeWall(r, width_ + 1, rng);
  }
}

void Level::placeWall(int r, int c, Rng& rng) {
  Tile& t = tile(r, c);
  if (t.cls == TileClass::FLOOR) return;
  if (rng.real() <= kVeinSpawnRate) {
    t.cls = TileClass::VEIN;
  } else if (rng.real() <= state_->soilSpawnRate) {
    t.cls = TileClass::SOIL;
    state_->soilSpawnRate = 0.6;  // soil clusters
  } else {
    t.cls = TileClass::WALL;
    state_->soilSpawnRate = 0.05;
  }
}

// The elevator stands on an inner floor tile (all eight neighbours open) of the start room, within two
// tiles of its middle.
void Level::placeElevator(Rng& rng) {
  Room& start = rooms_[static_cast<size_t>(rootRoom_ - 1)];
  std::vector<Pos> spots;
  for (int dr = -2; dr <= 2; ++dr)
    for (int dc = -2; dc <= 2; ++dc) {
      const Pos p{start.center.r + dr, start.center.c + dc};
      if (tile(p.r, p.c).roomId != start.id || tile(p.r, p.c).cls != TileClass::FLOOR) continue;
      bool inner = true;
      for (const Pos& q : adjacent(p.r, p.c))
        if (tile(q.r, q.c).cls != TileClass::FLOOR) inner = false;
      if (inner) spots.push_back(p);
    }
  Pos at = start.center;
  if (!spots.empty()) at = spots[static_cast<size_t>(rng.range(0, static_cast<std::int64_t>(spots.size()) - 1))];
  tile(at.r, at.c).cls = TileClass::ELEVATOR;
  start.hasStaircase = true;
  staircases_.push_back(at);
}

bool Level::isValidEntrance(int r, int c) const {
  return (tile(r + 1, c).isWall() && tile(r - 1, c).isWall()) || (tile(r, c + 1).isWall() && tile(r, c - 1).isWall());
}

void Level::addDoors(Rng& rng) {
  for (const Pos& e : entrances_) {
    if (tile(e.r, e.c).cls != TileClass::FLOOR || !isValidEntrance(e.r, e.c)) continue;
    tile(e.r, e.c).cls = rng.real() > 0.1 ? TileClass::C_DOOR : TileClass::O_DOOR;  // 10% start open
  }
}

// Breadth-first from a random room's middle to the nearest plain wall outside any room; that wall
// becomes the secret door.
void Level::placeSecretDoor(Rng& rng) {
  const int roomIndex = static_cast<int>(rng.range(1, static_cast<std::int64_t>(rooms_.size())));
  const Pos start = rooms_[static_cast<size_t>(roomIndex - 1)].center;
  std::vector<char> seen(matrix_.size(), 0);
  const auto idx = [&](Pos p) { return static_cast<size_t>(p.r + 1) * static_cast<size_t>(width_ + 2) + static_cast<size_t>(p.c); };
  std::deque<Pos> queue{start};
  seen[idx(start)] = 1;
  static constexpr Pos kDirs[4] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};  // up, down, left, right
  while (!queue.empty()) {
    const Pos pos = queue.front();
    queue.pop_front();
    const Tile& t = tile(pos.r, pos.c);
    if (t.cls == TileClass::WALL && t.roomId == 0) {
      tile(pos.r, pos.c).cls = TileClass::SECRET;
      secretX_ = pos.c;
      secretY_ = pos.r;
      secretSet_ = true;
      return;
    }
    for (const Pos& d : kDirs) {
      const Pos n{pos.r + d.r, pos.c + d.c};
      if (withinBounds(n.r, n.c, height_ - 1, width_ - 1) && !seen[idx(n)]) {
        seen[idx(n)] = 1;
        queue.push_back(n);
      }
    }
  }
}

}  // namespace laby
