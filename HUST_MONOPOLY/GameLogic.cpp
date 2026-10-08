#include <GameLogic.h>
#include <cstdlib>
#include <iostream>

using namespace std;

pair<int, int> rollDice(){
  return {(rand() % 6) + 1, rand() % 6 + 1};
}
void movePlayer(GameState& state, Player& player, int steps){
  int oldPos = player.position;
  player.position = (player.position + steps) % 40;
  if (player.position < oldPos){
    player.cash += 1000000;
  }
}
