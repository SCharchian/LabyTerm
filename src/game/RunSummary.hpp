#pragma once
#include <string>

namespace laby {

// How a run ended (what the game-over screen shows). Plain data, so it can be saved and later feed the leaderboards / the ghost.
struct RunSummary {
  std::string heroName;
  std::string killedBy;     // the monster's display name
  int killerLevel = 0;
  bool killerWasBoss = false;
  int floor = 0;            // where you died
  int heroLevel = 1;
  int steps = 0;
  int score = 0;            // as earned during the run
  int stepBonus = 0;        // steps * score.perStepTaken, added when the run ends (Score.lua kScorePerStepTaken)
  int finalScore = 0;       // score + stepBonus
  int rank = 0;             // its place in the top 10 (1-based), or 0 when it did not make it
};

}  // namespace laby
