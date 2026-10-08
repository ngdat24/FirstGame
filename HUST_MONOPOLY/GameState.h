#pragma once
#include <vector>
#include <player.h>
#include <tile.h>
using namespace std;

struct GameState {
  vector<Player> players;
  vector<Tile> Tiles;

  int currentTempo = 0;
  int CurrentPlayerIndex = 0;
};