#pragma once
#include <utility>
#include <GameState.h>

using namespace std;

pair<int, int> rollDice();
void movePlayer(GameState& state, Player& player, int steps);
void resolveLanding(GameState& state, Player& player, Tile& tile);

void buyTile(GameState& state, Player& player, Tile& tile);
void upgradeTile(GameState& state, Player& player, Tile& tile);
void payRent(GameState& state, Player& player, Tile& tile);
void attempSteal(GameState& state, Player& player, Tile& tile, long long bribeAmount);