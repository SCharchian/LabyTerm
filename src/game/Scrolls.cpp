#include "game/Scrolls.hpp"

#include <algorithm>

#include "core/Tuning.hpp"

namespace laby {

CastResult payForScroll(Hero& hero, Rng& rng, const Item& scroll, int floor) {
  if (hero.mp < scroll.mpCost) return CastResult::NoMana;
  hero.mp -= scroll.mpCost;
  const auto& t = tuning().scrolls;
  const double chance = std::max(t.disintegrateMinChance, t.disintegrateBaseChance - t.disintegrateIntWeight * hero.stat(Stat::INT) -
                                                              t.disintegrateDexWeight * hero.stat(Stat::DEX) + t.disintegratePerDungeonLevel * floor);
  if (!(rng.real() <= chance)) return CastResult::Cast;
  const ScrollType type = scroll.scroll;  // (`scroll` may point into the list being erased from)
  std::erase_if(hero.scrolls, [&](const Item& s) { return s.scroll == type; });
  return CastResult::Disintegrated;
}

}  // namespace laby
