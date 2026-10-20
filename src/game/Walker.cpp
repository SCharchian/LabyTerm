#include "game/Walker.hpp"

namespace laby {

void Walker::lift(Dungeon& d) { d.level().tile(y, x).cls = standingOn; }

void Walker::place(Dungeon& d, int px, int py, Direction f) {
  x = px;
  y = py;
  facing = f;
  standingOn = d.level().tile(py, px).cls;
  d.level().tile(py, px).cls = TileClass::PLAYER;
}

StepResult Walker::step(Dungeon& d, int dx, int dy, bool forward) {
  doorOpened = false;
  Level& lv = d.level();
  const int nx = x + dx, ny = y + dy;
  Tile& to = lv.tile(ny, nx);

  if (forward && to.cls == TileClass::SECRET) {  // only found by walking into it face on
    to.cls = TileClass::SECRET_FOUND;
    lv.secretOpen = false;
    return StepResult::FoundSecret;
  }
  if (forward && to.cls == TileClass::SECRET_FOUND) return StepResult::SecretOpenAlready;
  if (to.isWall()) return StepResult::Bumped;

  lv.tile(y, x).cls = standingOn;  // put back what the player was standing on
  x = nx;
  y = ny;
  standingOn = to.cls;
  if (standingOn == TileClass::C_DOOR) {  // walking into a closed door opens it
    standingOn = TileClass::O_DOOR;
    doorOpened = true;
  }
  to.cls = TileClass::PLAYER;
  return StepResult::Moved;
}

StepResult Walker::stepForward(Dungeon& d) {
  static constexpr int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};  // NORTH, EAST, SOUTH, WEST
  const int f = static_cast<int>(facing);
  return step(d, dx[f], dy[f], true);
}

StepResult Walker::stepBackward(Dungeon& d) {
  static constexpr int dx[4] = {0, -1, 0, 1}, dy[4] = {1, 0, -1, 0};
  const int f = static_cast<int>(facing);
  return step(d, dx[f], dy[f], false);
}

WallView::Input viewTiles(Dungeon& d, const Walker& w) {
  WallView::Input in;
  // For each facing: where "left of the row" is and which way is ahead (all in map x, y)
  const Walker::Basis b = Walker::basisFor(w.facing);
  const auto row = [&](int ahead) {
    std::array<TileClass, 3> r{};
    for (int i = -1; i <= 1; ++i) r[static_cast<size_t>(i + 1)] = d.getTileType(w.x + b.ax * ahead + b.sx * i, w.y + b.ay * ahead + b.sy * i);
    return r;
  };
  in.row0 = row(0);
  in.row1 = row(1);
  in.row2 = row(2);
  in.secretOpen = d.level().secretOpen;
  return in;
}

}  // namespace laby
