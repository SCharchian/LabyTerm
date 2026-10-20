#pragma once
#include <vector>

namespace laby {

// Which floors have had their boss killed and their level treasure looted (LevelTracker.lua; the original packs
// 32 floors into an integer). A cleared floor cannot be entered again.
class LevelTracker {
 public:
  explicit LevelTracker(int floors = 10000) : boss_(static_cast<size_t>(floors) + 1, false), treasure_(static_cast<size_t>(floors) + 1, false), fountain_(static_cast<size_t>(floors) + 1, false), secret_(static_cast<size_t>(floors) + 1, false) {}

  bool bossDone(int floor) const { return get(boss_, floor); }
  bool treasureDone(int floor) const { return get(treasure_, floor); }
  // Marks the boss / treasure of `floor` done. Returns true when this completed the floor (both are done now).
  bool markBoss(int floor) { return mark(boss_, floor) && treasureDone(floor); }
  bool markTreasure(int floor) { return mark(treasure_, floor) && bossDone(floor); }
  bool fountainDone(int floor) const { return get(fountain_, floor); }  // the floor's fountain has been drunk (it is drained)
  void markFountain(int floor) { mark(fountain_, floor); }
  // The floor's secret door has paid its score. Kept here, not on the Level: a floor is rebuilt whenever you come back to it.
  bool secretPaid(int floor) const { return get(secret_, floor); }
  bool markSecretPaid(int floor) { return mark(secret_, floor); }  // true the first time only

  // For saving: the floors marked in each list, and the way back.
  enum class Kind { Boss, Treasure, Fountain, Secret };
  std::vector<int> marked(Kind k) const {
    const std::vector<bool>& v = list(k);
    std::vector<int> out;
    for (size_t f = 1; f < v.size(); ++f) if (v[f]) out.push_back(static_cast<int>(f));
    return out;
  }
  void restore(Kind k, const std::vector<int>& floors) { for (int f : floors) mark(list(k), f); }

 private:
  const std::vector<bool>& list(Kind k) const { return k == Kind::Boss ? boss_ : k == Kind::Treasure ? treasure_ : k == Kind::Fountain ? fountain_ : secret_; }
  std::vector<bool>& list(Kind k) { return k == Kind::Boss ? boss_ : k == Kind::Treasure ? treasure_ : k == Kind::Fountain ? fountain_ : secret_; }
  static bool get(const std::vector<bool>& v, int f) { return f >= 1 && static_cast<size_t>(f) < v.size() && v[static_cast<size_t>(f)]; }
  // true if it was not marked before
  static bool mark(std::vector<bool>& v, int f) {
    if (f < 1 || static_cast<size_t>(f) >= v.size() || v[static_cast<size_t>(f)]) return false;
    v[static_cast<size_t>(f)] = true;
    return true;
  }
  std::vector<bool> boss_, treasure_, fountain_, secret_;
};

}  // namespace laby
