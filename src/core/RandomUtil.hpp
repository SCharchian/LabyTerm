#pragma once
#include <algorithm>
#include <cmath>

#include "core/Rng.hpp"
#include "core/Tuning.hpp"

namespace laby {

// helpFunctions.lua biasedRandom(): a number near `center` (triangular-ish; higher `strength` = more spread),
// clamped to [minValue, center + radius] and to maxValue. Kept in doubles, as Lua keeps these values.
inline double biasedRandom(Rng& rng, double center, double radius, double strength, double maxValue = 999, double minValue = 1) {
  radius = std::abs(radius);
  const double distance = rng.real() - 0.5;
  double value = std::floor(center + distance * strength + 0.5);
  value = std::max(minValue, std::min(center + radius, value));
  return std::min(maxValue, value);
}

// helpFunctions.lua globalMonsterStatLevelNormalize(): moves `base` up (or down, for negative `levels`) by about
// 1.65 per level on average (the growth range is the `scaling` tuning).
inline double statLevelNormalize(Rng& rng, double base, double levels) {
  const auto& s = tuning().scaling;
  if (levels == 0 || base == 0) return base;
  if (levels >= 0) {
    const double growth = static_cast<double>(rng.range(s.growthMinPercent, s.growthMaxPercent)) / 100;
    return base >= 0 ? std::floor(base + levels + growth * levels) : std::ceil(base + levels - growth * levels);
  }
  const double growth = 1 - static_cast<double>(rng.range(s.growthMinPercent, s.growthMaxPercent)) / 100;
  return base >= 0 ? std::floor(base + levels - growth * (levels / 2)) : std::ceil(base + levels + growth * levels);
}

}  // namespace laby
