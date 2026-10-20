// The town: prices, the Blacksmith's stock, buying, selling charmees and the temple.
#include <cstdio>

#include "game/Store.hpp"
#include "ui/WalkSession.hpp"

using namespace laby;

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAILED: %s (line %d)\n", #cond, __LINE__);           \
      return 1;                                                         \
    }                                                                   \
  } while (0)

int main() {
  // prices: the original's formulas
  Item sword;
  sword.type = EquipmentType::Weapon;
  sword.level = 4;
  sword.bonuses = {2, 1, 0, 0, 0};
  sword.ac = 1;
  CHECK(itemPrice(sword) == 4 * 5 + (2 + 1 + 1) * 3);
  Item junk;
  junk.level = 0;
  junk.bonuses = {-9, 0, 0, 0, 0};
  CHECK(itemPrice(junk) == 1);  // never below the minimum
  Item scroll = ItemData::get().scrolls[3];  // Magic Missile, type 4
  CHECK(itemPrice(scroll) == scroll.level * 5 + 4 * 5);
  Monster m;
  m.level = 3;
  m.hp = 10;
  m.stats = {10, 10, 10, 10, 10};
  CHECK(beastPrice(m) == 3 * 5 + 30);

  // the Blacksmith's stock: seven slots, the last a scroll, the rest equipment
  Rng rng;
  rng.setSeed(4);
  const auto shop = restockBlacksmith(rng, 6);
  CHECK(static_cast<int>(shop.size()) == kNumShopSlots);
  CHECK(shop.back().type == EquipmentType::Scroll);
  for (size_t i = 0; i + 1 < shop.size(); ++i) CHECK(shop[i].isEquipment());

  // buying, in the session: Town sign -> Blacksmith -> Enter buys the highlighted item
  WalkSession s(7);
  s.debugGiveKit();
  s.debugGoToTown();
  CHECK(s.inTown());
  const int gold0 = s.hero().gold;
  s.handle(Key::Down);                    // Blacksmith
  s.handle(Key::Enter);
  s.handle(Key::Enter);                   // its page
  s.handle(Key::Enter);                   // buy the first item
  CHECK(s.hero().gold < gold0);
  const int gold1 = s.hero().gold;
  // too poor: nothing is bought
  WalkSession p(7);
  p.debugGiveKit();
  p.debugGoToTown();
  p.handle(Key::Down);
  p.handle(Key::Enter);
  for (int i = 0; i < 20; ++i) p.handle(Key::Enter);  // buy until the gold runs out (the shop restocks the slot each time)
  CHECK(p.hero().gold >= 0);
  CHECK(gold1 >= 0);

  // BeastMart: the first Enter asks, the second sells
  WalkSession b(7);
  b.debugGiveKit();
  b.debugGoToTown();
  b.handle(Key::Down); b.handle(Key::Down);
  b.handle(Key::Enter);
  CHECK(b.hero().charmees.size() == 2);
  const int g = b.hero().gold;
  b.handle(Key::Enter);
  CHECK(b.hero().charmees.size() == 2 && b.hero().gold == g);  // only asked
  b.handle(Key::Enter);
  CHECK(b.hero().charmees.size() == 1 && b.hero().gold > g);

  // Temple: heals everything, free
  WalkSession t(7);
  t.debugGiveKit();
  t.debugGoToTown();
  const int gt = t.hero().gold;
  t.handle(Key::Enter);                   // Temple
  t.handle(Key::Enter);                   // heal
  CHECK(t.hero().hp == t.hero().maxHP && t.hero().mp == t.hero().maxMP && t.hero().gold == gt);
  // back to the sign, then the elevator
  t.handle(Key::Escape);                  // Esc steps back out of the Temple, it does not open the Esc menu
  CHECK(!t.menuOpen());
  t.handle(Key::Escape);                  // on the square there is nothing to back out of: the Esc menu
  CHECK(t.menuOpen());
  t.handle(Key::Escape);
  CHECK(!t.menuOpen());
  for (int i = 0; i < 3; ++i) t.handle(Key::Down);
  t.handle(Key::Enter);
  CHECK(!t.inTown() && t.inElevator());
  std::printf("store_test OK\n");
  return 0;
}
