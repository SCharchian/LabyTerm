#include "game/WallView.hpp"

#include <algorithm>
#include <stdexcept>

#include "core/DataFile.hpp"

namespace laby {
namespace {

using T = TileClass;

// RenderWalls()'s display list: add() puts a sprite on it (a no-op if it is already there), remove() takes it off.
struct DisplayList {
  struct Item { std::string id; int seq; };
  std::vector<Item> items;
  int next = 0;
  void add(const std::string& id) {
    for (const Item& i : items)
      if (i.id == id) return;
    items.push_back({id, next++});
  }
  void remove(const std::string& id) {
    items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& i) { return i.id == id; }), items.end());
  }
};

// is the tile a wall or door (as opposed to floor, an object, or something else)?
bool wallOrDoor(T t) { return t == T::WALL || t == T::C_DOOR || t == T::O_DOOR || t == T::SECRET || t == T::SECRET_FOUND; }

}  // namespace

WallView::WallView(std::vector<SpriteDef> defs) : defs_(std::move(defs)) {
  for (size_t i = 0; i < defs_.size(); ++i) byId_[defs_[i].id] = i;
}

WallView WallView::fromDataFile() {
  const nlohmann::json j = loadDataFile("view_sprites.json");
  std::vector<SpriteDef> defs;
  for (const auto& s : j.at("sprites")) defs.push_back({s.at("id"), s.at("image"), s.at("x"), s.at("y"), s.at("z")});
  WallView v(std::move(defs));
  v.background_ = j.at("background");
  const auto& h = j.at("hardEdge");
  v.hardEdge_ = {h.at("leftX"), h.at("rightX"), h.at("yTop"), h.at("yTopCombat"), h.at("yBottom")};
  return v;
}

const SpriteDef& WallView::def(const std::string& id) const {
  const auto it = byId_.find(id);
  if (it == byId_.end()) throw std::logic_error("WallView: unknown sprite id " + id);  // a typo in the port, caught at runtime
  return defs_[it->second];
}

WallView::Output WallView::render(const Input& in) const {
  DisplayList list;
  const auto& r0 = in.row0;
  const auto& r1 = in.row1;
  const auto& r2 = in.row2;

  // Work back to front: back walls/objects first.
  // BACK ROW, depth 2: left, centre, right
  static constexpr const char* kSide[3] = {"Left", "Center", "Right"};
  for (int i = 0; i < 3; ++i) {
    const std::string side = kSide[i];
    const T t = r2[static_cast<size_t>(i)];
    if (wallOrDoor(t)) {
      if (t == T::WALL || t == T::SECRET) list.add("_2sprite" + side + "BackWall");
      else if (t == T::C_DOOR) list.add("_2sprite" + side + "BackDoor");
      else if (t == T::O_DOOR) list.add("_2sprite" + side + "BackDoorOpen");
      else if (t == T::SECRET_FOUND) list.add("_2sprite" + side + "SecretOpen");
    } else if (t == T::ELEVATOR) list.add("_2sprite" + side + "ElevatorCenter");
    else if (t == T::BOSS) list.add("_2sprite" + side + "LevelBoss");
    else if (t == T::TREASURE) list.add("_2sprite" + side + "Chest_Level");
  }

  // MIDDLE ROW, depth 1: left side
  if (wallOrDoor(r1[0])) {
    if (wallOrDoor(r1[1])) {  // a wall in front of us: render doors face on, not turned
      if (r1[0] == T::C_DOOR) list.add("_1spriteFrontLeftDoorClosed");
      else if (r1[0] == T::O_DOOR) list.add("_1spriteFrontLeftDoorOpen");
      else if (r1[0] == T::SECRET_FOUND) list.add("_1spriteFrontLeftSecretOpen");
      else if (r0[0] == T::WALL) list.add("_1spriteCorrLeftWall");
      else list.add("_1spriteFrontLeftWall");
    } else {  // floor in front of us: render the door/object turned 90 degrees
      if (r1[0] == T::C_DOOR) {
        list.remove("_2spriteLeftBackWall");  // obscured and problematic
        list.add("_1spriteLeftDoorClosed");
      } else if (r1[0] == T::O_DOOR) {
        list.remove("_2spriteLeftBackWall");
        list.add("_1spriteLeftDoorOpen");
      } else if (r1[0] == T::ELEVATOR) list.add("_1spriteElevatorFrontLeft");
      else if (r1[0] == T::BOSS) list.add("_1spriteLevelBossLeft");
      else if (r1[0] == T::TREASURE) list.add("_1spriteLeftSideChest_Level");
      else if (r1[0] == T::SECRET_FOUND) {
        list.remove("_2spriteLeftBackWall");
        list.add("_1spriteLeftSecretOpen");
        list.add("_1spriteFrontLeftWall");
      } else {
        list.add("_1spriteFrontLeftWall");
        list.add("_1spriteCorrLeftWall");
      }
    }
  } else if (r1[0] == T::ELEVATOR) list.add("_1spriteElevatorFrontLeft");
  else if (r1[0] == T::BOSS) list.add("_1spriteLeftLevelBoss");
  else if (r1[0] == T::TREASURE) list.add("_1spriteLeftSideChest_Level");

  // right side
  if (wallOrDoor(r1[2])) {
    if (wallOrDoor(r1[1])) {
      if (r1[2] == T::C_DOOR) list.add("_1spriteFrontRightDoorClosed");
      else if (r1[2] == T::O_DOOR) list.add("_1spriteFrontRightDoorOpen");
      else if (r1[2] == T::SECRET_FOUND) list.add("_1spriteFrontRightSecretOpen");
      else if (r0[2] == T::WALL) list.add("_1spriteCorrRightWall");
      else list.add("_1spriteFrontRightWall");
    } else {
      if (r1[2] == T::C_DOOR) {
        list.remove("_2spriteRightBackWall");
        list.add("_1spriteRightDoorClosed");
      } else if (r1[2] == T::O_DOOR) {
        list.remove("_2spriteRightBackWall");
        list.add("_1spriteRightDoorOpen");
      } else if (r1[2] == T::ELEVATOR) {
        list.remove("_2spriteRightBackWall");
        list.add("_1spriteElevatorFrontRight");
      } else if (r1[2] == T::BOSS) {
        list.remove("_2spriteRightBackWall");
        list.add("_1spriteLevelBossRight");
      } else if (r1[2] == T::TREASURE) {
        list.remove("_2spriteRightBackWall");
        list.add("_1spriteRightSideChest_Level");
      } else if (r1[2] == T::SECRET_FOUND) {
        list.remove("_2spriteRightBackWall");
        list.add("_1spriteRightSecretOpen");
        list.add("_1spriteFrontRightWall");
      } else {
        list.add("_1spriteFrontRightWall");
        list.add("_1spriteCorrRightWall");
      }
    }
  } else if (r1[2] == T::ELEVATOR) list.add("_1spriteElevatorFrontRight");
  else if (r1[2] == T::BOSS) list.add("_1spriteRightLevelBoss");
  else if (r1[2] == T::TREASURE) list.add("_1spriteRightSideChest_Level");

  // The centre last: it may remove the sides' corridor panels
  if (wallOrDoor(r1[1])) {
    list.remove("_1spriteCorrLeftWall");  // a wall in front of us: no corridor panels
    list.remove("_1spriteCorrRightWall");
    if (r1[1] == T::C_DOOR) list.add("_1spriteFrontDoorClosed");
    else if (r1[1] == T::O_DOOR) list.add("_1spriteFrontCenterDoorOpen");
    else if (r1[1] == T::SECRET && in.secretFlicker) list.add("_1spriteFrontCenterWallSecret");  // closed secret door, glimpsed in the flicker
    else if (r1[1] == T::SECRET_FOUND && in.secretOpen) list.add("_1spriteFrontCenterWallSecretFound");
    else list.add("_1spriteFrontCenterWall");
    list.remove("_2spriteCenterBackWall");  // obscured by the front wall
    list.remove("_2spriteCenterBackDoor");
  } else if (r1[1] == T::ELEVATOR) list.add("_1spriteElevatorCenter");
  else if (r1[1] == T::BOSS) list.add("_1spriteLevelBossCenter");
  else if (r1[1] == T::TREASURE) list.add("_1spriteCenterChest_Level");

  // hard vertical edges at corners, to help the perspective
  Output out;
  const bool f0 = r1[0] == T::FLOOR, f1 = r1[1] == T::FLOOR, f2 = r1[2] == T::FLOOR;
  if ((f0 != f1) && r0[0] == T::FLOOR) out.hardLeft = true;
  if ((f2 != f1) && r0[2] == T::FLOOR) out.hardRight = true;
  if (r0[0] == T::WALL && r1[0] == T::FLOOR) out.hardLeft = true;
  if (r0[2] == T::WALL && r1[2] == T::FLOOR) out.hardRight = true;
  // not when an object (boss, elevator, level treasure) is in front of us
  for (T obj : {T::BOSS, T::ELEVATOR, T::TREASURE})
    if (r1[1] == obj || r1[0] == obj || r1[2] == obj) out.hardLeft = out.hardRight = false;

  // FRONT ROW, depth 0: side walls only
  if (wallOrDoor(r0[0])) {
    if (r0[0] == T::C_DOOR) list.add("_0spriteLeftDoor");
    else if (r0[0] == T::O_DOOR) list.add("_0spriteLeftDoorOpen");
    else if (r0[0] == T::SECRET_FOUND) list.add("_0spriteLeftSecretOpen");
    else list.add("_0spriteLeftWall");
  }
  if (wallOrDoor(r0[2])) {
    if (r0[2] == T::C_DOOR) list.add("_0spriteRightDoor");
    else if (r0[2] == T::O_DOOR) list.add("_0spriteRightDoorOpen");
    else if (r0[2] == T::SECRET_FOUND) list.add("_0spriteRightSecretOpen");
    else list.add("_0spriteRightWall");
  }

  // draw order: by z, then the order they were added (the Playdate drew sprites that way)
  std::vector<DisplayList::Item> items = list.items;
  std::stable_sort(items.begin(), items.end(), [&](const auto& a, const auto& b) { return def(a.id).z < def(b.id).z; });
  for (const auto& i : items) out.sprites.push_back(&def(i.id));
  return out;
}

}  // namespace laby
