#include "game/Monster.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "core/DataFile.hpp"
#include "core/RandomUtil.hpp"

namespace laby {
namespace {

StatTable parseStats(const nlohmann::json& j) {
  StatTable t{};
  for (const char* n : {"STR", "DEX", "INT", "VIT", "CHA"}) {
    const size_t i = static_cast<size_t>(n[0] == 'S' ? 0 : n[0] == 'D' ? 1 : n[0] == 'I' ? 2 : n[0] == 'V' ? 3 : 4);
    if (j.contains(n)) t[i] = j.at(n).get<double>();
  }
  return t;
}

MonsterData load() {
  const nlohmann::json j = loadDataFile("monsters.json");
  MonsterData d;
  for (const auto& p : j.at("prefixes")) {
    unsigned st = 0;
    for (const auto& n : p.at("Status")) {
      const std::string name = n.get<std::string>();
      if (name == "POISON") st |= static_cast<unsigned>(Ailment::Poison);
      else if (name == "SLEEP") st |= static_cast<unsigned>(Ailment::Sleep);
      else if (name == "BLIND") st |= static_cast<unsigned>(Ailment::Blind);
      else throw std::runtime_error("monsters.json: unknown prefix status " + name);
    }
    d.prefixes.push_back({p.at("Name").get<std::string>(), parseStats(p.at("stats")), st, p.value("Tier", 2)});
  }
  const char* names[] = {"HUMAN", "UNDEAD", "ANIMAL", "DEMON", "FAE"};
  for (int c = 0; c < kMonsterClassCount; ++c)
    for (const auto& m : j.at("classes").at(names[c]))
      d.byClass[static_cast<size_t>(c)].push_back(
          {m.at("DisplayName").get<std::string>(), static_cast<MonsterClass>(c), m.at("Image").get<std::string>(), parseStats(m.at("stats")), m.value("Hover", 0.0)});
  const std::pair<const char*, MonsterAction> ids[] = {{"attack", MonsterAction::Attack}, {"drain", MonsterAction::Drain}, {"bite", MonsterAction::Bite},
                                                       {"fire", MonsterAction::Fire},     {"lull", MonsterAction::Lull},   {"dust", MonsterAction::Dust}};
  for (int c = 0; c < kMonsterClassCount; ++c) {
    auto& list = d.actions[static_cast<size_t>(c)];
    if (j.contains("actions") && j.at("actions").contains(names[c]))
      for (const auto& a : j.at("actions").at(names[c])) {
        const std::string id = a.at("Id").get<std::string>();
        const auto at = std::find_if(std::begin(ids), std::end(ids), [&](const auto& e) { return id == e.first; });
        if (at == std::end(ids)) throw std::runtime_error("monsters.json: unknown action " + id);
        list.push_back({at->second, a.at("Weight").get<int>()});
      }
    if (list.empty()) list.push_back({MonsterAction::Attack, 1});
  }
  return d;
}

}  // namespace

const MonsterData& MonsterData::get() {
  static const MonsterData d = load();
  return d;
}

Monster Monster::create(Rng& rng, int floor, int level, bool boss, bool boss10) {
  const auto& t = tuning().monsters;
  const MonsterData& data = MonsterData::get();
  Monster m;
  m.isBoss = boss;
  if (boss) level = static_cast<int>(std::floor(t.bossLevelSlope * level + t.bossLevelOffset));  // BossLevelizer
  boss10 = boss10 && boss;
  m.isBoss10 = boss10;
  if (boss10) level += t.boss10LevelBonus + t.boss10LevelBonusPerTen * std::max(0, (t.boss10EveryFloors > 0 ? floor / t.boss10EveryFloors : 1) - 1);
  // the recovery floor: stats are made for a lower level than the monster's own (it still counts, scores, drops and gives XP as its level)
  const int statLevel = std::max(1, level - (floor > 1 && floor % 10 == 1 ? t.recoverySoftenLevels : 0));

  // RandomizeMonster: which class, which monster
  floor = std::max(floor, 1);
  const int slots[kMonsterClassCount] = {t.classSlot1, t.classSlot2, t.classSlot3, t.classSlot4, t.classSlot5};
  int cls = std::clamp(slots[(floor - 1) % kMonsterClassCount], 0, kMonsterClassCount - 1);
  if (!(rng.range(1, 100) < t.dominantClassRollBelow)) cls = static_cast<int>(rng.range(1, kMonsterClassCount)) - 1;
  const auto& list = data.byClass[static_cast<size_t>(cls)];
  const MonsterDef& def = list[static_cast<size_t>(rng.range(1, static_cast<std::int64_t>(list.size()))) - 1];
  m.displayName = def.displayName;
  m.cls = def.cls;
  m.image = def.image;
  m.hover = def.hover;
  m.stats = def.stats;

  // A prefix ("Hungry", "Cursed", ...)?
  double prefixChance = t.prefixChanceBase + t.prefixChancePerLevel * level;
  if (boss) prefixChance *= t.bossPrefixMultiplier;
  prefixChance = std::min<double>(prefixChance, t.prefixChanceCap);
  std::string prefixName;
  unsigned prefixStatus = 0;
  int prefixTier = 2;
  if (prefixChance > static_cast<double>(rng.range(1, 100))) {
    const MonsterPrefix& p = data.prefixes[static_cast<size_t>(rng.range(1, static_cast<std::int64_t>(data.prefixes.size()))) - 1];
    prefixName = p.name;
    prefixStatus = p.status;
    prefixTier = p.tier;
    for (size_t k = 0; k < kStatCount; ++k) m.stats[k] += p.stats[k] * statLevel * t.prefixStatPerLevel;
  }

  // Class bonuses and maluses
  for (size_t k = 0; k < kStatCount; ++k) m.stats[k] += t.classBonusPerLevel[static_cast<size_t>(cls)][k] * statLevel;

  // Turn the definition's stats into this monster's actual stats
  for (size_t k = 0; k < kStatCount; ++k) {
    m.stats[k] = biasedRandom(rng, t.statBase + m.stats[k] * t.statDefinitionWeight + statLevel * t.statLevelWeight,
                              t.statRadius + m.stats[k] * t.statDefinitionWeight, t.statStrength, t.statMax, statLevel);
    m.stats[k] = statLevelNormalize(rng, m.stats[k], ((statLevel - 1) + std::max(0, statLevel - t.deepGrowthStart) * t.deepGrowthExtra) * t.statGrowthFactor);
  }
  m.experience = static_cast<int>(std::floor(biasedRandom(rng, level, std::ceil(level / 2.0), t.expStrength, level * 2, 1)));

  // CreateByLevel: HP, AC
  double maxHP = biasedRandom(rng, m.stat(Stat::VIT), m.stat(Stat::VIT) / 2, t.hpStrength, t.hpMax, t.statBase);
  if (boss) maxHP *= 1 + static_cast<double>(rng.range(t.bossHpBonusMinPercent, t.bossHpBonusMaxPercent)) / 100;
  if (boss10) maxHP *= t.boss10HpMultiplier;
  m.level = level;
  m.ac = static_cast<int>(std::floor(m.stat(Stat::DEX) * t.acDexFactor + statLevel * t.acLevelFactor));
  m.maxHP = std::max(1, static_cast<int>(std::floor(std::floor(maxHP) * t.hpMultiplier + 1e-9)));
  m.hp = m.maxHP;
  if (boss10) m.experience = std::max(1, static_cast<int>(std::floor(m.experience * t.boss10XpMultiplier + 1e-9)));
  m.onHit = prefixStatus;
  if (!prefixName.empty()) {
    const double xpMul = prefixTier <= 1 ? t.prefixExpWeak : prefixTier >= 3 ? t.prefixExpStrong : t.prefixExpMiddle;
    m.experience = std::max(1, static_cast<int>(std::floor(m.experience * xpMul + 1e-9)));
    m.displayName = prefixName + " " + m.displayName;
  }
  if (boss10) {  // a scary name for the Boss10: cosmetic only (the kind, class and stats are the ones rolled above)
    static const char* const kTitles[] = {"Dread", "Grim", "Black", "Ashen", "Hollow", "Ruinous", "Nameless", "Undying", "Gloom", "Wretched",
                                          "Pale", "Bitter", "Blighted", "Sunken", "Howling", "Forsaken", "Crimson", "Ancient", "Ravenous", "Merciless"};
    static const char* const kNames[] = {"Malgrath", "Vorthak", "Zul'Karn", "Morthuun", "Gravelord", "Skarn", "Nyxaroth", "Bonegrinder", "Ulvane", "Drakmoor",
                                         "Hexen", "Korvax", "Thessaly", "Grimshaw", "Oblivar", "Maelstrom", "Ashmaw", "Sorrowfang", "Vexiel", "Doomwarden"};
    m.displayName = std::string(kTitles[rng.range(1, 20) - 1]) + " " + kNames[rng.range(1, 20) - 1];
  }
  return m;
}

}  // namespace laby
