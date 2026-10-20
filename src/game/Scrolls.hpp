#pragma once
#include "game/Hero.hpp"

namespace laby {

enum class CastResult { NoMana, Disintegrated, Cast };

// Scrolls.lua CastScroll / DidScrollDisintegrate: spends the scroll's MP, then rolls whether the scroll crumbles (the chance falls with
// INT and DEX and grows with the floor); a scroll that crumbles leaves the hero's list. `Cast` means the effect should happen.
CastResult payForScroll(Hero& hero, Rng& rng, const Item& scroll, int floor);

}  // namespace laby
