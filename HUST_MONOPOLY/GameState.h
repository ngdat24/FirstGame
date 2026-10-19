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
inline constexpr const char* kRulesVersion = "1.6";

struct GameConfig {
  long long startCash = 1000000;  // mean; actual starting cash also depends on turn order
  long long startCashOrderStep = 0;  // equal cash by default; nonzero only for balance experiments
  long long salary = 200000;     // collected when passing/landing on Start
  long long retakeFee = 50000;   // Retake Exam escape fee (voluntary or forced)
  long long adminFee = 20000;    // Special Corner teleport fee
  int jailTurns = 3;
  int maxDoublesInRow = 3;       // this many doubles in one turn -> jail
  int maxTempo = 48;             // game ends when this tempo concludes (8 semesters x 6 tempos)
  int tempoPerSemester = 6;      // 5 study tempos + 1 exam tempo per semester
  int examSeasonStart = 6;       // tempo 6 of each semester pays scholarships
  int rentFactor[6] = {0, 3, 6, 9, 15, 30};  // x baseRent, indexed by Grade: None,D,C,B,A,APlus
  int rentPercent = 100;         // scale the final rent, not the integer grade factor
  long long auctionStartPrice = 50000;
  long long auctionEscalation = 20000;
  long long auctionRent = 25000;
  long long examBonusA = 150000;
  long long examBonusAPlus = 400000;
  int stealBonusTempos = 3;      // the "Phao thi" bonus expires after this many tempos
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
  int auctionTileId = -1;  // offer takeover after this visit fee is fully paid
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
