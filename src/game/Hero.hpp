#pragma once
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Rng.hpp"
#include "core/TuningParams.hpp"
#include "game/Item.hpp"
#include "game/Monster.hpp"

namespace laby {

// The player character (Player.lua), without equipment and scrolls for now: derived values come from the base
// stats alone. `calc` is what combat reads (the original's calculatedStats).
struct Hero {
  std::string name = "D. Crawley";
  int level = 1, experience = 0;
  int hp = 10, maxHP = 10, mp = 10, maxMP = 10, ac = 0, gold = 0;
  int score = 0, steps = 0;  // score as in Score.lua (steps are added when the game ends)
  StatTable base{}, calc{};
  std::vector<Monster> charmees;  // up to two monsters fighting on the player's side
  std::array<std::optional<Item>, 8> equipment;  // by EquipmentType (Weapon .. Necklace)
  std::vector<Item> scrolls;                     // one of each kind, ordered by scroll type
  std::optional<Item> startingItem;              // what character creation granted (shown there only)
  bool invisible = false;                        // Invisibility: most monsters don't notice you (until it wears off)
  int invisibleSince = 0;                        // the step count when it was cast

  double stat(Stat s) const { return calc[static_cast<size_t>(s)]; }

  // Player:RandomizeStats(): every stat random 8..18, full health.
  void roll(Rng& rng);
  // The two halves of roll(), for re-rolling on the character creation screen. rollStartingItem replaces all gear and scrolls.
  void rollStats(Rng& rng);
  void rollStartingItem(Rng& rng);
  // Player:RecalculateALLStats(): stats, max HP / MP and AC from the base stats plus all equipment; if the maximums
  // grew, the current values grow with them.
  void recalc();
  // Player:GetExpNeededForLevelUp(level)
  static int expNeeded(int level);
  // Player:CheckForLevelUp(): true if a level was gained (the perk then still has to be chosen: applyPerk).
  bool gainLevel(const std::function<void(const std::string&)>& say);
  // Player:DoLevelUp: choice 0..4 raises that stat by 1; anything else is "<Random> +2" (progression.levelUpStatPoints stats at random).
  void applyPerk(int choice, Rng& rng, const std::function<void(const std::string&)>& say);
  // Both at once with the random perk, for headless runs and tests.
  bool checkLevelUp(Rng& rng, const std::function<void(const std::string&)>& say);
  void removeDeadCharmees();
  // main.lua: after each step, 1 HP and 1 MP come back every `every` steps (0 = never).
  void stepRegen(int every);

  bool hasScroll(ScrollType t) const;
  void addScroll(const Item& scroll);  // keeps scrolls ordered by type (AddAndSortScroll)
  void equip(const Item& item);        // replaces whatever is in that slot, then recalculates
  // Drinks a health or mana potion (Loot.lua LootAPotion): restores a random share of the maximum. Returns the amount.
  int drink(Rng& rng, bool health);
};

}  // namespace laby
