#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/Dungeon.hpp"
#include "game/Fountain.hpp"
#include "game/Ghost.hpp"
#include "game/Hero.hpp"
#include "game/Item.hpp"
#include "game/LevelTracker.hpp"

namespace laby {

// What lives on disk between sessions (all versioned JSON, written through a temporary file):
//   save.json       a saved game: written from the town sign post only, deleted the moment it is loaded (single use)
//   graveyard.json  the ghost of the last hero who died, deleted when the next run begins
//   scores.json     the top 10
// Default folder: $LABYTERM_STATE_DIR, else $XDG_STATE_HOME/labyterm, else ~/.local/state/labyterm.
std::string stateDir();

inline constexpr int kSaveVersion = 1;
inline constexpr int kFloorSeeds = 100;

// A saved game: you can only save in town, so no floor is "current". Every floor is regenerated from its own seed (kFloorSeeds of them,
// recycled after that), and the floors visited so far carry a FloorMemory on top (seen tiles, opened chests, the secret door).
struct SaveData {
  std::uint64_t seed = 0;
  std::string rng;  // Rng::state() text; empty (e.g. a save from an older version) means: start a fresh sequence from the seed
  std::vector<std::uint64_t> floorSeeds;  // kFloorSeeds entries; floor f uses floorSeeds[(f - 1) % kFloorSeeds]
  std::map<int, FloorMemory> floors;      // by floor number
  Hero hero;
  LevelTracker tracker;
  std::vector<Item> shop;
  Fountain fountain;
  std::optional<Ghost> ghost;  // a ghost met on this run's way down has not been met yet
};

enum class LoadResult { Ok, Missing, Corrupt };

bool saveExists(const std::string& dir);
bool writeSave(const std::string& dir, const SaveData& data);
// Reads save.json and deletes it (a save is used once). A file that cannot be read is renamed save.corrupt.json.
LoadResult loadSave(const std::string& dir, SaveData& out);

bool writeGhost(const std::string& dir, const Ghost& ghost);
// Reads graveyard.json and deletes it: you get one chance at your ghost.
std::optional<Ghost> takeGhost(const std::string& dir);

struct ScoreEntry {
  std::string name;
  int score = 0, floor = 0, level = 0, steps = 0;
};
inline constexpr size_t kLegends = 10;
// Puts `e` into the list (best first, at most kLegends): above the first entry it beats strictly, so a tie keeps the older run ahead.
// Returns its rank (1-based), or 0 when it did not make it (a score of 0 never does).
int insertScore(std::vector<ScoreEntry>& list, const ScoreEntry& e);
std::vector<ScoreEntry> loadScores(const std::string& dir);
// Records a finished run: returns its rank, or 0.
int recordScore(const std::string& dir, const ScoreEntry& e);

}  // namespace laby
