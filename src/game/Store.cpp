#include "game/Store.hpp"

#include <algorithm>
#include <cmath>

#include "core/Tuning.hpp"

namespace laby {

int itemPrice(const Item& item) {
  const auto& t = tuning().store;
  int price = item.level * t.pricePerLevel;
  if (item.type == EquipmentType::Scroll) {
    price += (static_cast<int>(item.scroll) + 1) * t.scrollPricePerType;  // the original's scroll types count from 1
  } else {
    int bonus = item.hp + item.ac + item.mp;
    for (int b : item.bonuses) bonus += b;
    price += bonus * t.priceMultiplier;
  }
  return std::max(price, t.minPrice);
}

int beastPrice(const Monster& m) {
  const auto& t = tuning().store;
  double total = m.hp;
  for (double s : m.stats) total += s;
  const double price = m.level * t.beastPricePerLevel + total / t.beastStatDivisor;
  return std::max(static_cast<int>(std::ceil(price)), t.minPrice);
}

Item newStockItem(Rng& rng, int heroLevel, bool scroll) {
  const EquipmentType type = scroll ? EquipmentType::Scroll : static_cast<EquipmentType>(rng.range(1, kNumEquipmentTypes) - 1);
  return randomItem(rng, type, heroLevel);
}

std::vector<Item> restockBlacksmith(Rng& rng, int heroLevel) {
  std::vector<Item> shop;
  for (int i = 0; i < tuning().store.itemsInShop - 1; ++i) shop.push_back(i == 0 && tuning().store.masterPiece ? masterPiece(rng, heroLevel) : newStockItem(rng, heroLevel, false));
  shop.push_back(newStockItem(rng, heroLevel, true));
  return shop;
}

}  // namespace laby
