#pragma once

#include <string>
#include <vector>
using namespace std;
struct Player {
  int id;
  string name;
  long long cash = 1000000;
  int position = 0;
  vector<int> ownedTileIds;
  int turnInJail = 0;
  int consecutiveDoubles = 0;
  bool BankRupt = false;
};