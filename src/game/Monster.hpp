#pragma once
#include <array>
#include <string>
#include <vector>

#include "core/Enums.hpp"
#include "core/Rng.hpp"
#include "core/TuningParams.hpp"

namespace laby {

// One entry of data/monsters.json: a kind of monster before level scaling.
struct MonsterDef {
  std::string displayName;
  MonsterClass cls = MonsterClass::HUMAN;
  std::string image;  // as in the original, e.g. "images/Monsters/Human/Ninja"
  StatTable stats{};
  double hover = 0;   // flying monsters: how far the sprite floats above the floor, as a fraction of its own height (the shadow stays on the floor)
};

// What a monster can do to you beyond damage (bits). Prefixes carry them (data/monsters.json "Status"); Combat applies them.
enum class Ailment : unsigned { Poison = 1, Sleep = 2, Blind = 4 };

// A monster's choice of action on its turn (data/monsters.json "actions", by class).
enum class MonsterAction { Attack, Drain, Bite, Fire, Lull, Dust };

struct MonsterPrefix {
  std::string name;
  StatTable stats{};
  unsigned status = 0;  // Ailment bits its hits can inflict
  int tier = 2;         // power tier: 1 weak, 2 middle, 3 strong (sets the XP multiplier; data/monsters.json "Tier")
};

struct ActionWeight {
  MonsterAction action = MonsterAction::Attack;
  int weight = 1;
};

// All monster kinds and prefixes (loaded once from data/monsters.json).
struct MonsterData {
  std::array<std::vector<MonsterDef>, kMonsterClassCount> byClass;
  std::vector<MonsterPrefix> prefixes;
  std::array<std::vector<ActionWeight>, kMonsterClassCount> actions;  // never empty: at least the plain attack
  static const MonsterData& get();
};

// A monster in play (Monster.lua). Stats and HP are generated for a level; see Monster::create.
struct Monster {
  std::string displayName;
  MonsterClass cls = MonsterClass::HUMAN;
  std::string image;
  StatTable stats{};
  double hover = 0;  // see MonsterDef
  int level = 1, ac = 0, maxHP = 1, hp = 1, experience = 1;
  bool isBoss = false, isBoss10 = false, fled = false, charmed = false;
  unsigned status = 0;  // StatusFlag bits
  unsigned onHit = 0;   // Ailment bits its hits can inflict (from its prefix)

  double stat(Stat s) const { return stats[static_cast<size_t>(s)]; }
  bool has(StatusFlag f) const { return (status & static_cast<unsigned>(f)) != 0; }
  void set(StatusFlag f) { status |= static_cast<unsigned>(f); }
  void clear(StatusFlag f) { status &= ~static_cast<unsigned>(f); }

  // Monster:init / CreateByLevel / RandomizeMonster: the random draws happen in the original's order.
  // `floor` picks the floor's dominant monster class; `level` is the dungeon level the monster is made for
  // (bosses are made at a higher level).
  // `boss10`: the floor's boss is the every-10th-floor Boss10 (a bigger, tougher boss; needs boss = true). On the recovery floor after it (floor % 10 == 1)
  // the monsters' stats are made for `monsters.recoverySoftenLevels` levels lower while their level, XP and loot stay the floor's.
  static Monster create(Rng& rng, int floor, int level, bool boss, bool boss10 = false);
};

}  // namespace laby
