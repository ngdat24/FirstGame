#pragma once

#include <random>
#include <utility>
#include <vector>

#include "GameState.h"

// Everything a player (human, bot, or network client) can ask the game to do.
enum class ActionType {
  RollDice,          // AwaitRoll
  PayRetakeFee,      // AwaitRoll, only while in jail
  Buy,               // AwaitLandingChoice on an unowned Institute
  Upgrade,           // AwaitLandingChoice on your own Institute
  Decline,           // AwaitLandingChoice (not on an opponent's tile) / AwaitCornerChoice
  PayRent,           // AwaitLandingChoice on an opponent's Institute
  AttemptSteal,      // AwaitLandingChoice on an opponent's Institute (uses amount)
  TeleportUnowned,   // AwaitCornerChoice (uses tileId)
  TeleportOwned,     // AwaitCornerChoice (uses tileId)
  SellTile           // AwaitLiquidation (uses tileId)
};

struct Action {
  ActionType type = ActionType::RollDice;
  int playerId = -1;
  long long amount = 0;  // bribe for AttemptSteal
  int tileId = -1;       // target tile for teleport / sell
};

// ---- Setup -----------------------------------------------------------------
GameState makeNewGame(int numPlayers, const std::vector<Tile>& board);

// ---- Main entry point ------------------------------------------------------
// Validates and applies one action. Returns false (and changes nothing) if the
// action is illegal for the current phase / player. The rng is passed in, so
// the server owns all randomness and simulations are reproducible by seed.
bool applyAction(GameState& state, const Action& action, std::mt19937& rng);

// Applies an already-rolled pair of dice (what RollDice does after rolling).
// Public so tests and a server can inject specific dice values.
bool applyRoll(GameState& state, int d1, int d2);

// ---- Building blocks (also usable on their own) ----------------------------
std::pair<int, int> rollDice(std::mt19937& rng);
void movePlayer(GameState& state, Player& player, int steps);
void resolveLanding(GameState& state, Player& player, Tile& tile);

bool buyTile(GameState& state, Player& player, Tile& tile);
bool upgradeTile(GameState& state, Player& player, Tile& tile);
long long rentFor(const GameState& state, const Tile& tile);
// Assumes the bribe was validated (0 < bribe <= cash). Returns true on success.
bool attemptSteal(GameState& state, Player& player, Tile& tile,
                  long long bribeAmount, std::mt19937& rng);

bool cornerHasOptions(const GameState& state, const Player& player);
void examSeasonPayout(GameState& state);
long long calculateNetWorth(const GameState& state, const Player& player);
int countAlivePlayers(const GameState& state);
