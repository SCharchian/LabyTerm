#pragma once
#include <cstdint>
#include <random>
#include <sstream>
#include <string>

namespace laby {

// The game's random numbers: a seedable std::mt19937_64, so a seed gives a repeatable run and a saved game can carry on its sequence.
class Rng {
 public:
  explicit Rng(std::uint64_t seed = 0) { setSeed(seed); }

  void setSeed(std::uint64_t seed) { eng_.seed(seed); }

  // The generator's whole state as text, so a saved game can carry on the same sequence. setState returns false (and changes
  // nothing) when the text is not a valid state.
  std::string state() const {
    std::ostringstream o;
    o << eng_;
    return o.str();
  }
  bool setState(const std::string& s) {
    std::istringstream i(s);
    std::mt19937_64 e;
    if (!(i >> e)) return false;
    eng_ = e;
    return true;
  }

  // A raw 64-bit value (for deriving seeds)
  std::uint64_t next() { return eng_(); }

  // A float in [0, 1)
  double real() { return std::generate_canonical<double, 53>(eng_); }

  // An integer in [lo, hi] (inclusive); lo if hi < lo
  std::int64_t range(std::int64_t lo, std::int64_t hi) {
    if (hi <= lo) return lo;
    return std::uniform_int_distribution<std::int64_t>(lo, hi)(eng_);
  }

 private:
  std::mt19937_64 eng_;
};

}  // namespace laby
