#pragma once
#include <vector>

#include "core/Rng.hpp"
#include "game/Item.hpp"
#include "game/Monster.hpp"

namespace laby {

// The town's shops (Blacksmith.lua, BeastMart.lua).

// Blacksmith.lua CalculatePrice: gold per item level, plus for scrolls a step per scroll type, for equipment a price per bonus point.
int itemPrice(const Item& item);
// BeastMart.lua CalculatePrice: what BeastMart pays for a charmee.
int beastPrice(const Monster& monster);

inline constexpr int kNumShopSlots = 7;  // `store.itemsInShop`
// One new item of the hero's level for a shop slot: a random kind of equipment, or a scroll.
Item newStockItem(Rng& rng, int heroLevel, bool scroll);
// ReStockBlacksmith(): every slot new; the last one is always a scroll.
std::vector<Item> restockBlacksmith(Rng& rng, int heroLevel);

}  // namespace laby
