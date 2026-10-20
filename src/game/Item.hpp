#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/Enums.hpp"
#include "core/Rng.hpp"

namespace laby {

// Loot (Equipment.lua, Loot.lua): wearable equipment, scrolls and potions share one type, as in the original.
struct Item {
  std::string name;         // "Club"
  std::string prefix;       // "Flame", or empty
  std::string displayName;  // prefix + name
  EquipmentType type = EquipmentType::Weapon;
  WeaponType weaponType = WeaponType::BLUNT;  // weapons only
  int level = 0, ac = 0, hp = 0, mp = 0;
  std::array<int, 5> bonuses{};  // STR DEX INT VIT CHA

  // Scrolls only (data/scrolls.json)
  ScrollType scroll = ScrollType::MagicArrow;
  std::array<std::string, 3> desc;
  int levelReq = 0, intReq = 0, mpCost = 0;
  double disintegrate = 0;
  bool costsTurn = false, combat = false, utility = false;

  bool masterwork = false;  // a guaranteed prefixed, slightly higher-level find (the Blacksmith's master's piece, a Boss10's drop); drawn in gold

  bool isEquipment() const { return static_cast<int>(type) < 8; }
  int bonus(Stat s) const { return bonuses[static_cast<size_t>(s)]; }
};

struct ItemPrefix {
  std::string name;
  int ac = 0, hp = 0, mp = 0;
  std::array<int, 5> bonuses{};
};

// The item tables from data/equipment.json and data/scrolls.json.
struct ItemData {
  std::array<std::vector<Item>, 8> equipment;  // by EquipmentType (Weapon .. Necklace)
  std::vector<Item> scrolls;
  std::vector<ItemPrefix> weaponPrefixes, armorPrefixes, accessoryPrefixes;
  static const ItemData& get();
};

// Equipment.lua GetRandomItem(): a random item of `type` for item level `level` (a list entry near the level, a
// level near it, maybe a prefix, random stat variation, then Loot.lua globalItemLeveler). Potions are plain.
// `forcePrefix`: always carries a prefix (equipment only).
Item randomItem(Rng& rng, EquipmentType type, int level, bool forcePrefix = false);

// An empty-handed placeholder (GetEMPTYItem), for comparing against an empty slot.
Item emptyItem(EquipmentType type);

// What a chest holds (Loot.lua GenerateTreasureByLevel).
struct Treasure {
  double gold = 0;
  Item item;
  int level = 0;
};
// `levelTreasure`: the floor's level chest or a boss chest (juiced up a bit). `hurt` / `lowMana` let a regular chest
// turn into an instant health / mana potion with some chance.
// `boss10`: a Boss10's chest: a prefixed equipment piece of level `level` .. `level` + 10% (a masterwork).
Treasure generateTreasure(Rng& rng, int level, bool levelTreasure, bool hurt, bool lowMana, bool boss10 = false);
// A Blacksmith's master's piece for a hero of this level: prefixed equipment, item level `level` .. `level` + 10% (rounded up), a masterwork.
Item masterPiece(Rng& rng, int level);

}  // namespace laby
