#pragma once

#include <string>
#include <vector>

struct Player {
  int id = 0;  // invariant: players[i].id == i
  std::string name;
  long long cash = 0;  // makeNewGame() sets this from GameConfig::startCash
  int position = 0;
  std::vector<int> ownedTileIds;
  int turnsInJail = 0;         // > 0 means currently in the Retake Exam room
  int consecutiveDoubles = 0;  // doubles rolled so far in the CURRENT turn
  bool isBankrupt = false;     // true = expelled

  // Status effects (set by cards).
  int skipTurns = 0;           // how many of this player's upcoming turns are skipped
  bool halveNextDice = false;  // next movement is halved (rounded down)
  int stealBonusPercent = 0;   // extra % on the next steal attempt, used up by it
};
