#pragma once

#include <random>
#include <string>
#include <utility>
#include <vector>

#include "card.h"
#include "player.h"
#include "tile.h"

// All tunable numbers live here so simulations can change them without
// touching the rules code.
struct GameConfig {
  long long startCash = 700000;
  long long salary = 200000;     // collected when passing/landing on Start
  long long retakeFee = 50000;   // Retake Exam escape fee (voluntary or forced)
  long long adminFee = 50000;    // Special Corner teleport fee
  int jailTurns = 3;
  int maxDoublesInRow = 3;       // this many doubles in one turn -> jail
  int maxTempo = 48;             // game ends when this tempo concludes (8 semesters x 6 tempos)
  int tempoPerSemester = 6;      // 5 study tempos + 1 exam tempo per semester
  int examSeasonStart = 6;       // tempo 6 of each semester pays scholarships
  int rentFactor[6] = {0, 3, 6, 9, 18, 40};  // x baseRent, indexed by Grade: None,D,C,B,A,APlus
  long long examBonusA = 50000;
  long long examBonusAPlus = 100000;
  int stealBonusTempos = 3;      // the "Phao thi" bonus expires after this many tempos (the tempo it is drawn in counts as the first)
  bool cardsEnabled = true;      // false: the card tiles do nothing (for A/B simulations)
};

// What the game is waiting for. Exactly one player (the current one) acts.
enum class Phase {
  AwaitRoll,           // khoảng tgian roll
  AwaitLandingChoice,  // khoảng tgian lựa chọn landing
  AwaitCornerChoice,   // góc đặc biệt
  AwaitLiquidation,    // khoảng tgian thanh lí
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
  // Who starts each tempo. makeNewGame() picks it at random so that nobody owns the
  // first-mover advantage; a tempo ends when the turn comes back to this seat.
  int firstPlayerIndex = 0;
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
