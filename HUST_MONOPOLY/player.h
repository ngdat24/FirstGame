#pragma once

#include <string>
#include <vector>

struct Player {
  int id = 0;  // invariant: players[i].id == i
  std::string name;
  long long cash = 1000000;
  int position = 0;
  std::vector<int> ownedTileIds;
  int turnsInJail = 0;         // > 0 means currently in the Retake Exam room
  int consecutiveDoubles = 0;  // doubles rolled so far in the CURRENT turn
  bool isBankrupt = false;     // true = expelled
};
