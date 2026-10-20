#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/TuningParams.hpp"

namespace laby {

// Gameplay tuning. Defaults come from data/gameplay.json; an optional per-user override file
// (partial, same shape) wins over them:
//     $LABYTERM_TUNING, else $XDG_CONFIG_HOME/labyterm/tuning.json, else ~/.config/labyterm/tuning.json
// Game code reads values via tuning(); never cache them across frames, so live edits take effect.
const Tuning& tuning();

std::string tuningOverridePath();

struct TuningLoad {
  Tuning values;
  std::vector<std::string> overridden;  // paths that came from the override file
  std::string overrideError;            // non-empty: the override file was rejected (defaults used)
};

// Loads defaults + overrides. Throws std::runtime_error only if the defaults file is broken;
// an invalid override file is reported in `overrideError` and ignored.
TuningLoad loadTuning();

// Installs `load` as the active tuning (what tuning() returns).
void setTuning(const TuningLoad& load);

// Prints every knob with value, doc and override status ("*" marks overridden knobs).
std::string describeTuning(const TuningLoad& load);

// Watches data/gameplay.json and the override file; reloads when either changes.
class TuningWatcher {
 public:
  explicit TuningWatcher(unsigned intervalMs = 500) : intervalMs_(intervalMs) {}
  // True when tuning was (re)loaded or the override started/stopped failing. On a bad edit the
  // previous values stay active and lastError() says what was wrong.
  bool poll(std::uint64_t nowMs);
  const std::string& lastError() const { return error_; }
  int overriddenCount() const { return overridden_; }

 private:
  unsigned intervalMs_;
  std::uint64_t lastCheckMs_ = 0;
  long long stamp_ = -1;
  bool primed_ = false;
  std::string error_;
  int overridden_ = 0;
};

}  // namespace laby
