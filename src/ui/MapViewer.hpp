#pragma once
#include <cstdint>

namespace laby {

// `labyterm --map [seed]`: a developer view of the dungeon generator. Shows the whole floor (no fog).
//   Enter/Z/Space  new random dungeon      Up  deeper floor      Down  shallower floor
//   S              show/hide the secret door     Esc  quit
int runMapViewer(std::uint64_t seed);

}  // namespace laby
