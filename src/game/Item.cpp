#include "game/Item.hpp"

#include <algorithm>
#include <cmath>

#include "core/DataFile.hpp"
#include "core/RandomUtil.hpp"

namespace laby {
namespace {

constexpr int kStatNames = 5;

std::array<int, 5> parseBonuses(const nlohmann::json& j, int* hp = nullptr, int* mp = nullptr) {
  std::array<int, 5> b{};
  static const char* kName[] = {"STR", "DEX", "INT", "VIT", "CHA"};
  for (int i = 0; i < kStatNames; ++i)
    if (j.contains(kName[i])) b[static_cast<size_t>(i)] = j.at(kName[i]).get<int>();
  if (hp && j.contains("HP")) *hp = j.at("HP").get<int>();
  if (mp && j.contains("MP")) *mp = j.at("MP").get<int>();
  return b;
}

std::vector<ItemPrefix> parsePrefixes(const nlohmann::json& list) {
  std::vector<ItemPrefix> out;
  for (const auto& p : list) {
    ItemPrefix x;
    x.name = p.at("Name").get<std::string>();
    x.ac = p.at("AC").get<int>();
    x.bonuses = parseBonuses(p.at("Bonuses"), &x.hp, &x.mp);
    out.push_back(x);
  }
  return out;
}

ItemData load() {
  ItemData d;
  const nlohmann::json eq = loadDataFile("equipment.json");
  for (int t = 0; t < 8; ++t) {
    const auto type = static_cast<EquipmentType>(t);
    for (const auto& j : eq.at("items").at(std::string(toString(type)))) {
      Item it;
      it.name = j.at("Name").get<std::string>();
      it.type = type;
      it.level = j.at("Level").get<int>();
      it.ac = j.at("AC").get<int>();
      it.hp = j.at("HP").get<int>();
      it.mp = j.at("MP").get<int>();
      if (j.contains("WeaponType")) it.weaponType = fromString<WeaponType>(j.at("WeaponType").get<std::string>()).value_or(WeaponType::BLUNT);
      it.bonuses = parseBonuses(j.at("Bonuses"));
      d.equipment[static_cast<size_t>(t)].push_back(it);
    }
  }
  d.weaponPrefixes = parsePrefixes(eq.at("prefixes").at("weapon"));
  d.armorPrefixes = parsePrefixes(eq.at("prefixes").at("armor"));
  d.accessoryPrefixes = parsePrefixes(eq.at("prefixes").at("accessory"));
  const nlohmann::json scrollFile = loadDataFile("scrolls.json");
  for (const auto& j : scrollFile.at("scrolls")) {
    Item s;
    s.type = EquipmentType::Scroll;
    s.name = s.displayName = j.at("Name").get<std::string>();
    s.scroll = fromString<ScrollType>(j.at("ScrollType").get<std::string>()).value_or(ScrollType::MagicArrow);
    s.desc = {j.at("Desc1").get<std::string>(), j.at("Desc2").get<std::string>(), j.at("Desc3").get<std::string>()};
    s.level = j.at("Level").get<int>();
    s.levelReq = j.at("LevelReq").get<int>();
    s.intReq = j.at("IntReq").get<int>();
    s.mpCost = j.at("MPCost").get<int>();
    s.disintegrate = j.at("DisintegratePercentage").get<double>();
    s.costsTurn = j.at("fCostsTurn").get<bool>();
    s.combat = j.at("fIsCombatScroll").get<bool>();
    s.utility = j.at("fIsUtilityScroll").get<bool>();
    d.scrolls.push_back(s);
  }
  return d;
}

// A yes/no draw with probability p.
bool flip(Rng& rng, double p) { return rng.real() < p; }

// Loot.lua globalItemLeveler(): spreads `levels` +1 points over the item's non-zero stats and grows HP, MP and AC.
void levelItem(Rng& rng, Item& item, double levels) {
  const auto& t = tuning().items;
  std::array<int, 5> original = item.bonuses;
  std::vector<int> nonZero;
  for (int s = 0; s < kStatNames; ++s)
    if (original[static_cast<size_t>(s)] != 0) nonZero.push_back(s);
  if (nonZero.empty()) {
    // No stats at all: pick two random stats to bump (weakly), and halve the levels
    nonZero.push_back(static_cast<int>(rng.range(1, 5)) - 1);
    nonZero.push_back(static_cast<int>(rng.range(1, 5)) - 1);
    original[static_cast<size_t>(nonZero[0])] = 1;
    original[static_cast<size_t>(nonZero[1])] = 1;
    levels = std::ceil(levels * t.noStatLevelPenalty);
  }
  const auto step = [&] { return flip(rng, t.levelerUpgradeChance) ? 1 : 0; };
  const auto second = [&] { return flip(rng, t.levelerSecondUpgradeChance); };
  for (int i = 0; i < static_cast<int>(std::abs(levels)); ++i) {
    const int mapped = nonZero[static_cast<size_t>(rng.range(1, static_cast<std::int64_t>(nonZero.size()))) - 1];
    int& b = item.bonuses[static_cast<size_t>(mapped)];
    if (original[static_cast<size_t>(mapped)] > 0) {
      b += step();
      if (original[static_cast<size_t>(mapped)] > 1 && second()) b += step();
    } else {
      b -= step();
      if (original[static_cast<size_t>(mapped)] < -1 && second()) b -= step();
    }
  }
  const int grow = static_cast<int>(std::floor(levels / t.upscaleLevelsDivisor));
  const auto upscale = [&](int v) {
    if (v > 0) {
      v += grow;
      if (v > 1 && flip(rng, t.upscaleDoubleChance)) v += grow;
    } else if (v < 0) {
      v -= grow;
      if (v < -1 && flip(rng, t.upscaleDoubleChance)) v -= grow;
    }
    return v;
  };
  item.hp = upscale(item.hp);
  item.mp = upscale(item.mp);
  item.ac = upscale(item.ac);
}

}  // namespace

const ItemData& ItemData::get() {
  static const ItemData d = load();
  return d;
}

Item emptyItem(EquipmentType type) {
  Item i;
  i.name = i.displayName = "NONE";
  i.type = type;
  return i;
}

Item randomItem(Rng& rng, EquipmentType type, int itemLevel, bool forcePrefix) {
  const auto& t = tuning().items;
  const ItemData& data = ItemData::get();
  Item item;
  if (type == EquipmentType::Health_Potion || type == EquipmentType::Mana_Potion) {
    item.type = type;
    item.name = item.displayName = type == EquipmentType::Health_Potion ? "Health Potion" : "Mana Potion";
    return item;
  }
  const std::vector<Item>& list = type == EquipmentType::Scroll ? data.scrolls : data.equipment[static_cast<size_t>(type)];
  const double center = t.indexCenterModulus > 0 ? itemLevel % t.indexCenterModulus : itemLevel;
  const int index = static_cast<int>(biasedRandom(rng, center, t.levelRadius, t.levelRadiusStrength, static_cast<double>(list.size()), 1));
  item = list[static_cast<size_t>(index) - 1];
  if (type != EquipmentType::Scroll) {
    item.level = static_cast<int>(biasedRandom(rng, itemLevel, t.levelSpread, t.levelRadiusStrength, itemLevel + t.levelMaxAbove, 1));
    if (forcePrefix || item.level + t.prefixChanceBase > rng.range(1, 100)) {
      const std::vector<ItemPrefix>& plist = type == EquipmentType::Weapon ? data.weaponPrefixes
                                             : (type == EquipmentType::Ring || type == EquipmentType::Necklace) ? data.accessoryPrefixes
                                                                                                                : data.armorPrefixes;
      const ItemPrefix& p = plist[static_cast<size_t>(rng.range(1, static_cast<std::int64_t>(plist.size()))) - 1];
      item.prefix = p.name;
      for (size_t s = 0; s < 5; ++s) item.bonuses[s] += p.bonuses[s];
      item.ac += p.ac;
      item.mp += p.mp;
      item.hp += p.hp;
    }
    for (size_t s = 0; s < 5; ++s) {
      const int value = item.bonuses[s];
      if (value != 0) {
        item.bonuses[s] = static_cast<int>(biasedRandom(rng, value, std::ceil(static_cast<double>(item.level) / t.statRadiusDivisor),
                                                        t.levelRadiusStrength, value + item.level, value - item.level));
      } else if (rng.real() <= t.zeroStatGainChance) {
        item.bonuses[s] = static_cast<int>(biasedRandom(rng, 0, 1, t.levelRadiusStrength * t.zeroStatGainStrengthMultiplier, 1, -1));
      }
    }
    levelItem(rng, item, itemLevel - 1);
  }
  item.displayName = item.prefix.empty() ? item.name : item.prefix + " " + item.name;
  return item;
}

Item masterPiece(Rng& rng, int level) {
  const int bump = static_cast<int>(rng.range(0, static_cast<std::int64_t>(std::ceil(level * 0.1))));
  Item item = randomItem(rng, static_cast<EquipmentType>(rng.range(1, kNumEquipmentTypes) - 1), level + bump, true);
  item.masterwork = true;
  return item;
}

Treasure generateTreasure(Rng& rng, int level, bool levelTreasure, bool hurt, bool lowMana, bool boss10) {
  const auto& l = tuning().loot;
  double lvl = level;
  if (levelTreasure) lvl = statLevelNormalize(rng, lvl, std::floor(1 + lvl / l.levelTreasureBoostDivisor));
  Treasure tr;
  const double radius = l.goldRadiusBase + lvl * l.goldRadiusPerLevel;
  tr.gold = biasedRandom(rng, l.goldBase + lvl, radius, radius, (lvl + 1) * l.goldMaxPerLevel, 1) * l.killGoldMultiplier;
  const int itemLevel = static_cast<int>(lvl);
  tr.item = randomItem(rng, static_cast<EquipmentType>(rng.range(1, 9) - 1), itemLevel);
  if (boss10) tr.item = masterPiece(rng, itemLevel);
  if (!levelTreasure) {
    // 20% chance (original: math.random(1,5) == 1) of an instant potion when you need one
    const auto potion = [&] { return l.potionDropChance == 0.2 ? rng.range(1, 5) == 1 : rng.real() < l.potionDropChance; };
    // (the roll is still made when it cannot apply, so the random sequence is the same either way; the boss10's masterwork is promised)
    if (hurt) {
      if (potion() && !boss10) tr.item = randomItem(rng, EquipmentType::Health_Potion, itemLevel);
    } else if (lowMana) {
      if (potion() && !boss10) tr.item = randomItem(rng, EquipmentType::Mana_Potion, itemLevel);
    }
  }
  tr.level = tr.item.level;
  return tr;
}

}  // namespace laby
