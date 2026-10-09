#pragma once

#include <random>
#include <string>
#include <utility>
#include <vector>

#include "card.h"
#include "player.h"
#include "tile.h"

// All tunable numbers live here so simulations can change them without
// touching the rules code. The economy numbers (start cash, salary, rent) are
// the preset chosen after the balance simulations; the rest are still
// placeholders.
struct GameConfig {
  long long startCash = 700000;
  long long salary = 100000;    // collected when passing/landing on Start
  long long retakeFee = 50000;  // Retake Exam escape fee (voluntary or forced)
  long long adminFee = 20000;   // Special Corner teleport fee
  int jailTurns = 3;
  int maxDoublesInRow = 3;      // this many doubles in one turn -> jail
  int maxTempo = 64;            // game ends when this tempo concludes
  int tempoPerSemester = 16;
  int examSeasonStart = 13;     // tempos 13..16 of each semester pay scholarships
  int rentFactor[5] = {0, 2, 4, 8, 16};  // x baseRent, indexed by Grade: None,D,C,B,A
  long long examBonusB = 20000;
  long long examBonusA = 50000;
  bool cardsEnabled = true;     // false: the card tiles do nothing (for A/B simulations)
};

// What the game is waiting for. Exactly one player (the current one) acts.
enum class Phase {
  AwaitRoll,           // roll dice (or pay the retake fee if in jail)
  AwaitLandingChoice,  // decide about the tile in pendingTileId
  AwaitCornerChoice,   // Special Corner: teleport somewhere or decline
  AwaitLiquidation,    // owes money, must sell Institutes until covered
  GameOver
};

struct PendingDebt {
  long long amount = 0;
  int creditorId = -1;  // -1 = bank
  int moveSteps = 0;    // >0: move this many steps once the debt is paid
};

struct GameState {
  GameConfig config;
  std::vector<Player> players;
  std::vector<Tile> tiles;

  int currentTempo = 1;
  int currentPlayerIndex = 0;
  Phase phase = Phase::AwaitRoll;
  int pendingTileId = -1;
  PendingDebt debt;
  int winnerId = -1;
  std::pair<int, int> lastDice{0, 0};

  // Card system. One shared deck drawn on every TileType::Other tile.
  // deckRng only shuffles the deck; it is server-side state and must never be
  // sent to clients (they could predict the next cards).
  CardDeck deck;
  std::mt19937 deckRng;
  int rentFreeTurnsLeft = 0;  // > 0: no rent is charged (Rent Holiday card)

  // Optional human-readable event log (UI / debugging). Off by default so
  // simulations stay fast.
  bool logEnabled = false;
  std::vector<std::string> eventLog;
};
