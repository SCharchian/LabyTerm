#include "game/Hero.hpp"

#include <algorithm>
#include <cmath>

#include "core/Tuning.hpp"

namespace laby {

void Hero::rollStats(Rng& rng) {
  for (auto& b : base) b = static_cast<double>(rng.range(tuning().player.statRollMin, tuning().player.statRollMax));
  recalc();
  hp = maxHP;
  mp = maxMP;
}

void Hero::rollStartingItem(Rng& rng) {
  equipment = {};
  scrolls.clear();
  // Player:RandomizeStats(): one random starting item (or scroll)
  const auto type = static_cast<EquipmentType>(rng.range(1, kEquipmentTypeCount - 2) - 1);
  const Item start = randomItem(rng, type, tuning().player.startingItemLevel);
  if (type == EquipmentType::Scroll) addScroll(start);
  else equipment[static_cast<size_t>(type)] = start;
  startingItem = start;
  recalc();
  hp = maxHP;
  mp = maxMP;
}

// A new run: the two halves above (the same ones the character screen's Re-Roll uses, so the two cannot drift apart), on a clean slate.
void Hero::roll(Rng& rng) {
  level = tuning().player.level;
  experience = tuning().player.experience;
  gold = tuning().player.gold;
  charmees.clear();
  score = steps = 0;
  rollStats(rng);
  rollStartingItem(rng);
}

void Hero::recalc() {
  const int origMaxHP = maxHP, origMaxMP = maxMP;
  const auto& pr = tuning().progression;
  int sumHP = 0, sumMP = 0, sumAC = 0;
  std::array<int, 5> bonus{};
  for (const auto& slot : equipment) {
    if (!slot) continue;
    for (size_t k = 0; k < 5; ++k) bonus[k] += slot->bonuses[k];
    sumHP += slot->hp;
    sumMP += slot->mp;
    sumAC += slot->ac;
  }
  for (size_t k = 0; k < 5; ++k) calc[k] = std::floor(base[k] + bonus[k]);
  maxHP = static_cast<int>(std::floor(calc[static_cast<size_t>(Stat::VIT)] * pr.hpPerVit)) + sumHP;
  maxMP = static_cast<int>(std::floor(calc[static_cast<size_t>(Stat::INT)] * pr.mpPerInt + sumMP));
  ac = static_cast<int>(std::floor(calc[static_cast<size_t>(Stat::DEX)] * pr.acPerDex + sumAC));
  if (maxHP > origMaxHP) hp += maxHP - origMaxHP;
  if (maxMP > origMaxMP) mp += maxMP - origMaxMP;
  hp = std::min(hp, maxHP);
  mp = std::min(mp, maxMP);
}

int Hero::expNeeded(int level) {
  const auto& p = tuning().progression;
  return static_cast<int>(std::floor(p.xpCoefficient * (1 + std::pow(level - 1, p.xpExponent)))) + p.xpFlat;
}

bool Hero::gainLevel(const std::function<void(const std::string&)>& say) {
  if (experience < expNeeded(level)) return false;
  ++level;
  score += tuning().score.perPlayerLevel * level;  // Score_LevelUp
  say("LEVEL UP! You are now level " + std::to_string(level) + ".");
  return true;
}

void Hero::applyPerk(int choice, Rng& rng, const std::function<void(const std::string&)>& say) {
  if (choice >= 0 && choice < 5) {  // Player:DoLevelUp: the stat you picked, +1
    base[static_cast<size_t>(choice)] += 1;
    say(std::string(statLongName(static_cast<size_t>(choice))) + " +1");
  } else {  // "<Random> +2": Labyrinth chooses
    std::string text = "Labyrinth chooses:";
    for (int i = 0; i < tuning().progression.levelUpStatPoints; ++i) {
      const size_t k = static_cast<size_t>(rng.range(1, 5)) - 1;
      base[k] += 1;
      text += std::string(i ? " AND " : " ") + statLongName(k) + " +1";
    }
    say(text);
  }
  recalc();
  if (const int heal = tuning().progression.levelUpHealPercent; heal > 0) {
    hp = std::min(maxHP, hp + maxHP * heal / 100);
    say("The surge of power heals you.");
  }
}

bool Hero::checkLevelUp(Rng& rng, const std::function<void(const std::string&)>& say) {
  if (!gainLevel(say)) return false;
  applyPerk(5, rng, say);  // no screen to choose on: Labyrinth chooses
  return true;
}

bool Hero::hasScroll(ScrollType t) const {
  return std::any_of(scrolls.begin(), scrolls.end(), [&](const Item& s) { return s.scroll == t; });
}

void Hero::addScroll(const Item& scroll) {
  const auto at = std::find_if(scrolls.begin(), scrolls.end(), [&](const Item& s) { return s.scroll > scroll.scroll; });
  scrolls.insert(at, scroll);
}

void Hero::equip(const Item& item) {
  equipment[static_cast<size_t>(item.type)] = item;
  recalc();
}

int Hero::drink(Rng& rng, bool health) {
  const auto& l = tuning().loot;
  int& cur = health ? hp : mp;
  const int max = health ? maxHP : maxMP;
  int amount = static_cast<int>(rng.range(static_cast<std::int64_t>(std::ceil(max * l.potionMinPercent / 100.0)),
                                          static_cast<std::int64_t>(std::ceil(max * l.potionMaxPercent / 100.0))));
  amount = std::min(amount, max - cur);
  cur += amount;
  return amount;
}

void Hero::removeDeadCharmees() {
  std::erase_if(charmees, [](const Monster& m) { return m.hp < 1; });
}

void Hero::stepRegen(int every) {
  if (every <= 0 || steps % every != 0) return;
  hp = std::min(hp + 1, maxHP);
  mp = std::min(mp + 1, maxMP);
}

}  // namespace laby
