#include "GameLogic.h"

#include <algorithm>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
void logEvent(GameState& s, const std::string& msg) {
  if (s.logEnabled) s.eventLog.push_back(msg);
}

Player& currentPlayer(GameState& s) { return s.players[s.currentPlayerIndex]; }

const char* gradeName(Grade g) {
  switch (g) {
    case Grade::D: return "D";
    case Grade::C: return "C";
    case Grade::B: return "B";
    case Grade::A: return "A";
    default: return "-";
  }
}

int findTileOfType(const GameState& s, TileType type) {
  for (const Tile& t : s.tiles)
    if (t.type == type) return t.id;
  return -1;
}

void removeOwned(Player& p, int tileId) {
  p.ownedTileIds.erase(
      std::remove(p.ownedTileIds.begin(), p.ownedTileIds.end(), tileId),
      p.ownedTileIds.end());
}

// Tile goes back to the bank: unowned, no grade, no invested value.
void releaseTileToBank(GameState& s, Tile& t) {
  if (t.ownerId >= 0) removeOwned(s.players[t.ownerId], t.id);
  t.ownerId = -1;
  t.grade = Grade::None;
  t.totalInvested = 0;
}

// Forward declarations (the turn flow is mutually recursive).
void endGame(GameState& s);
void nextTurn(GameState& s);
void finishLanding(GameState& s);
void moveAndResolve(GameState& s, Player& p, int steps);
void settleDebt(GameState& s);
void expel(GameState& s, Player& p);
void chargeOrLiquidate(GameState& s, long long amount, int creditorId, int moveSteps);
void sendToRetakeExam(GameState& s, Player& p);

// ---------------------------------------------------------------------------
// Turn flow
// ---------------------------------------------------------------------------
void endGame(GameState& s) {
  s.phase = Phase::GameOver;
  s.pendingTileId = -1;
  s.debt = PendingDebt{};
  s.winnerId = -1;
  long long bestWorth = -1;
  long long bestCash = -1;
  // Ascending iteration + strict comparisons: ties go to higher cash, then
  // to the lower player id.
  for (const Player& p : s.players) {
    if (p.isBankrupt) continue;
    const long long w = calculateNetWorth(s, p);
    if (w > bestWorth || (w == bestWorth && p.cash > bestCash)) {
      bestWorth = w;
      bestCash = p.cash;
      s.winnerId = p.id;
    }
  }
  if (s.winnerId >= 0)
    logEvent(s, "GAME OVER. Winner: " + s.players[s.winnerId].name +
                    " (net worth " + std::to_string(bestWorth) + ")");
}

void nextTurn(GameState& s) {
  if (countAlivePlayers(s) <= 1) {
    endGame(s);
    return;
  }
  const int n = static_cast<int>(s.players.size());
  const int old = s.currentPlayerIndex;
  int idx = old;
  do {
    idx = (idx + 1) % n;
  } while (s.players[idx].isBankrupt);

  s.currentPlayerIndex = idx;
  s.phase = Phase::AwaitRoll;
  s.pendingTileId = -1;
  s.debt = PendingDebt{};
  s.players[idx].consecutiveDoubles = 0;

  if (idx <= old) {  // wrapped around: every living player has played this tempo
    if (s.currentTempo >= s.config.maxTempo) {
      endGame(s);
      return;
    }
    ++s.currentTempo;
    logEvent(s, "--- Tempo " + std::to_string(s.currentTempo) + " begins ---");
    examSeasonPayout(s);
  }
}

// Called when the player has finished resolving everything about a roll.
void finishLanding(GameState& s) {
  s.pendingTileId = -1;
  s.debt = PendingDebt{};
  Player& p = currentPlayer(s);
  if (p.isBankrupt) {
    nextTurn(s);
    return;
  }
  if (p.consecutiveDoubles > 0) {  // last roll was a (non-triple) double
    s.phase = Phase::AwaitRoll;
    logEvent(s, p.name + " rolled doubles and rolls again.");
  } else {
    nextTurn(s);
  }
}

void moveAndResolve(GameState& s, Player& p, int steps) {
  movePlayer(s, p, steps);
  resolveLanding(s, p, s.tiles[p.position]);
}

void sendToRetakeExam(GameState& s, Player& p) {
  const int jail = findTileOfType(s, TileType::RetakeExam);
  if (jail >= 0) p.position = jail;
  p.turnsInJail = s.config.jailTurns;
  p.consecutiveDoubles = 0;  // no extra roll after being sent to jail
  logEvent(s, p.name + " is sent to the Retake Exam room.");
}

// ---------------------------------------------------------------------------
// Debt, liquidation, expulsion
// ---------------------------------------------------------------------------
void settleDebt(GameState& s) {
  Player& p = currentPlayer(s);
  const long long amount = s.debt.amount;
  const int creditor = s.debt.creditorId;
  const int steps = s.debt.moveSteps;
  p.cash -= amount;
  if (creditor >= 0) s.players[creditor].cash += amount;
  s.debt = PendingDebt{};
  if (steps > 0)
    moveAndResolve(s, p, steps);
  else
    finishLanding(s);
}

void expel(GameState& s, Player& p) {
  // All tiles were already sold. Whatever cash is left goes to the creditor.
  const long long leftover = std::max(0LL, p.cash);
  if (s.debt.creditorId >= 0) s.players[s.debt.creditorId].cash += leftover;
  p.cash = 0;
  p.isBankrupt = true;
  p.consecutiveDoubles = 0;
  p.turnsInJail = 0;
  logEvent(s, p.name + " is EXPELLED (cannot cover the debt).");
  s.debt = PendingDebt{};
  nextTurn(s);
}

// A forced payment. Pays at once if affordable; otherwise the player must
// sell Institutes (Phase::AwaitLiquidation), or is expelled if nothing is left.
void chargeOrLiquidate(GameState& s, long long amount, int creditorId, int moveSteps) {
  Player& p = currentPlayer(s);
  s.debt = PendingDebt{amount, creditorId, moveSteps};
  if (p.cash >= amount) {
    settleDebt(s);
    return;
  }
  if (p.ownedTileIds.empty()) {
    expel(s, p);
    return;
  }
  s.phase = Phase::AwaitLiquidation;
  logEvent(s, p.name + " owes " + std::to_string(amount) +
                  " and must sell Institutes.");
}

// ---------------------------------------------------------------------------
// Per-phase action handlers
// ---------------------------------------------------------------------------
bool handleRollPhase(GameState& s, Player& p, const Action& a, std::mt19937& rng) {
  switch (a.type) {
    case ActionType::RollDice: {
      const std::pair<int, int> dice = rollDice(rng);
      return applyRoll(s, dice.first, dice.second);
    }
    case ActionType::PayRetakeFee:
      if (p.turnsInJail <= 0 || p.cash < s.config.retakeFee) return false;
      p.cash -= s.config.retakeFee;
      p.turnsInJail = 0;
      logEvent(s, p.name + " pays the retake fee and leaves the Retake Exam room.");
      return true;  // still this player's AwaitRoll: they now roll normally
    default:
      return false;
  }
}

bool handleLandingChoice(GameState& s, Player& p, const Action& a, std::mt19937& rng) {
  if (s.pendingTileId < 0) return false;
  Tile& t = s.tiles[s.pendingTileId];
  const bool unowned = (t.ownerId == -1);
  const bool mine = (t.ownerId == p.id);
  const bool opponents = !unowned && !mine;

  switch (a.type) {
    case ActionType::Buy:
      if (!unowned || !buyTile(s, p, t)) return false;
      finishLanding(s);
      return true;
    case ActionType::Upgrade:
      if (!mine || !upgradeTile(s, p, t)) return false;
      finishLanding(s);
      return true;
    case ActionType::Decline:
      if (opponents) return false;  // must pay rent or attempt a steal
      finishLanding(s);
      return true;
    case ActionType::PayRent:
      if (!opponents) return false;
      chargeOrLiquidate(s, rentFor(s, t), t.ownerId, 0);
      return true;
    case ActionType::AttemptSteal:
      if (!opponents) return false;
      if (a.amount <= 0 || a.amount > p.cash) return false;  // cash only, no loans
      attemptSteal(s, p, t, a.amount, rng);
      finishLanding(s);
      return true;
    default:
      return false;
  }
}

bool handleCornerChoice(GameState& s, Player& p, const Action& a) {
  const long long fee = s.config.adminFee;
  switch (a.type) {
    case ActionType::Decline:
      finishLanding(s);
      return true;
    case ActionType::TeleportUnowned: {
      if (a.tileId < 0 || a.tileId >= static_cast<int>(s.tiles.size())) return false;
      Tile& t = s.tiles[a.tileId];
      if (t.type != TileType::Institute || t.ownerId != -1) return false;
      if (p.cash < fee + t.basePrice) return false;
      p.cash -= fee;
      p.position = t.id;  // teleporting does NOT pass Start: no salary
      logEvent(s, p.name + " teleports to " + t.name + ".");
      buyTile(s, p, t);
      finishLanding(s);
      return true;
    }
    case ActionType::TeleportOwned: {
      if (a.tileId < 0 || a.tileId >= static_cast<int>(s.tiles.size())) return false;
      Tile& t = s.tiles[a.tileId];
      if (t.type != TileType::Institute || t.ownerId != p.id) return false;
      if (t.grade == Grade::A || t.grade == Grade::None) return false;
      if (p.cash < fee + t.upgradeCost) return false;
      p.cash -= fee;
      p.position = t.id;
      logEvent(s, p.name + " teleports to " + t.name + ".");
      upgradeTile(s, p, t);
      finishLanding(s);
      return true;
    }
    default:
      return false;
  }
}

bool handleLiquidation(GameState& s, Player& p, const Action& a) {
  if (a.type != ActionType::SellTile) return false;
  if (a.tileId < 0 || a.tileId >= static_cast<int>(s.tiles.size())) return false;
  Tile& t = s.tiles[a.tileId];
  if (t.type != TileType::Institute || t.ownerId != p.id) return false;

  const long long proceeds = t.totalInvested / 2;  // 50% of total invested value
  logEvent(s, p.name + " sells " + t.name + " to the bank for " +
                  std::to_string(proceeds) + ".");
  releaseTileToBank(s, t);
  p.cash += proceeds;

  if (p.cash >= s.debt.amount)
    settleDebt(s);
  else if (p.ownedTileIds.empty())
    expel(s, p);
  return true;
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================
GameState makeNewGame(int numPlayers, const std::vector<Tile>& board) {
  GameState s;
  s.tiles = board;
  for (int i = 0; i < numPlayers; ++i) {
    Player p;
    p.id = i;
    p.name = "Player " + std::to_string(i + 1);
    p.cash = s.config.startCash;
    s.players.push_back(p);
  }
  return s;
}

std::pair<int, int> rollDice(std::mt19937& rng) {
  std::uniform_int_distribution<int> die(1, 6);
  const int a = die(rng);
  const int b = die(rng);
  return {a, b};
}

void movePlayer(GameState& s, Player& p, int steps) {
  const int boardSize = static_cast<int>(s.tiles.size());
  const int oldPos = p.position;
  p.position = (oldPos + steps) % boardSize;
  if (p.position < oldPos) {  // wrapped past (or onto) Start
    p.cash += s.config.salary;
    logEvent(s, p.name + " passes Start and collects " +
                    std::to_string(s.config.salary) + ".");
  }
}

long long rentFor(const GameState& s, const Tile& t) {
  return static_cast<long long>(t.baseRent) *
         s.config.rentFactor[static_cast<int>(t.grade)];
}

bool buyTile(GameState& s, Player& p, Tile& t) {
  if (t.type != TileType::Institute || t.ownerId != -1) return false;
  if (p.cash < t.basePrice) return false;
  p.cash -= t.basePrice;
  t.ownerId = p.id;
  t.grade = Grade::D;
  t.totalInvested = t.basePrice;
  p.ownedTileIds.push_back(t.id);
  logEvent(s, p.name + " registers for " + t.name + " (grade D) for " +
                  std::to_string(t.basePrice) + ".");
  return true;
}

bool upgradeTile(GameState& s, Player& p, Tile& t) {
  if (t.type != TileType::Institute || t.ownerId != p.id) return false;
  if (t.grade == Grade::None || t.grade == Grade::A) return false;
  if (p.cash < t.upgradeCost) return false;
  p.cash -= t.upgradeCost;
  t.totalInvested += t.upgradeCost;
  t.grade = static_cast<Grade>(static_cast<int>(t.grade) + 1);
  logEvent(s, p.name + " upgrades " + t.name + " to grade " + gradeName(t.grade) + ".");
  return true;
}

bool attemptSteal(GameState& s, Player& p, Tile& t, long long bribe, std::mt19937& rng) {
  // Success chance = bribe / total invested value of the tile, capped at 100%.
  double rate = 1.0;
  if (t.totalInvested > 0)
    rate = std::min(1.0, static_cast<double>(bribe) / t.totalInvested);

  p.cash -= bribe;  // the bribe is spent whether or not the steal works
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  const bool success = unit(rng) < rate;  // strict '<': a 0% chance never wins

  if (success) {
    removeOwned(s.players[t.ownerId], t.id);
    t.ownerId = p.id;  // grade and totalInvested are kept
    p.ownedTileIds.push_back(t.id);
    logEvent(s, p.name + " STEALS " + t.name + " (bribe " + std::to_string(bribe) + ").");
  } else {
    // Single penalty: the bribe is lost, no rent is owed.
    logEvent(s, p.name + " is caught cheating on " + t.name + " and loses the bribe " +
                    std::to_string(bribe) + ".");
  }
  return success;
}

bool cornerHasOptions(const GameState& s, const Player& p) {
  const long long avail = p.cash - s.config.adminFee;
  if (avail < 0) return false;
  for (const Tile& t : s.tiles) {
    if (t.type != TileType::Institute) continue;
    if (t.ownerId == -1 && t.basePrice <= avail) return true;
    if (t.ownerId == p.id && t.grade != Grade::A && t.grade != Grade::None &&
        t.upgradeCost <= avail)
      return true;
  }
  return false;
}

void resolveLanding(GameState& s, Player& p, Tile& t) {
  switch (t.type) {
    case TileType::AcademicWarning:
      logEvent(s, p.name + " lands on Academic Warning.");
      sendToRetakeExam(s, p);
      finishLanding(s);
      return;

    case TileType::SpecialCorner:
      if (cornerHasOptions(s, p)) {
        s.phase = Phase::AwaitCornerChoice;
        s.pendingTileId = t.id;
      } else {
        logEvent(s, p.name + " lands on the Special Corner but has no valid option.");
        finishLanding(s);
      }
      return;

    case TileType::Institute:
      if (t.ownerId == -1) {
        if (p.cash >= t.basePrice) {
          s.phase = Phase::AwaitLandingChoice;
          s.pendingTileId = t.id;
        } else {
          finishLanding(s);  // cannot afford it: nothing to decide
        }
      } else if (t.ownerId == p.id) {
        if (t.grade != Grade::A && p.cash >= t.upgradeCost) {
          s.phase = Phase::AwaitLandingChoice;
          s.pendingTileId = t.id;
        } else {
          finishLanding(s);
        }
      } else {
        s.phase = Phase::AwaitLandingChoice;  // pay rent or attempt a steal
        s.pendingTileId = t.id;
      }
      return;

    default:  // Start, RetakeExam (just visiting), Other
      finishLanding(s);
      return;
  }
}

void examSeasonPayout(GameState& s) {
  const int inSemester = ((s.currentTempo - 1) % s.config.tempoPerSemester) + 1;
  if (inSemester < s.config.examSeasonStart) return;
  for (Player& p : s.players) {
    if (p.isBankrupt) continue;
    long long bonus = 0;
    for (int id : p.ownedTileIds) {
      const Grade g = s.tiles[id].grade;
      if (g == Grade::A) bonus += s.config.examBonusA;
      if (g == Grade::B) bonus += s.config.examBonusB;
    }
    if (bonus > 0) {
      p.cash += bonus;
      logEvent(s, p.name + " receives a scholarship of " + std::to_string(bonus) + ".");
    }
  }
}

long long calculateNetWorth(const GameState& s, const Player& p) {
  long long worth = p.cash;
  for (int id : p.ownedTileIds) worth += s.tiles[id].totalInvested;
  return worth;
}

int countAlivePlayers(const GameState& s) {
  int n = 0;
  for (const Player& p : s.players)
    if (!p.isBankrupt) ++n;
  return n;
}

bool applyRoll(GameState& s, int d1, int d2) {
  if (s.phase != Phase::AwaitRoll || s.players.empty()) return false;
  if (d1 < 1 || d1 > 6 || d2 < 1 || d2 > 6) return false;

  Player& p = currentPlayer(s);
  s.lastDice = {d1, d2};
  const bool isDouble = (d1 == d2);
  const int steps = d1 + d2;
  logEvent(s, p.name + " rolls " + std::to_string(d1) + " + " + std::to_string(d2) +
                  (isDouble ? " (doubles)." : "."));

  // --- In the Retake Exam room: escape only with doubles (or forced fee) ----
  if (p.turnsInJail > 0) {
    if (isDouble) {
      p.turnsInJail = 0;
      p.consecutiveDoubles = 0;  // escaping with doubles gives no extra roll
      logEvent(s, p.name + " rolled doubles and escapes the Retake Exam room.");
      moveAndResolve(s, p, steps);
    } else {
      --p.turnsInJail;
      if (p.turnsInJail == 0) {
        logEvent(s, p.name + " failed the retake 3 times: forced to pay the fee.");
        chargeOrLiquidate(s, s.config.retakeFee, -1, steps);  // then moves
      } else {
        nextTurn(s);
      }
    }
    return true;
  }

  // --- Normal roll ---------------------------------------------------------
  if (isDouble) {
    ++p.consecutiveDoubles;
    if (p.consecutiveDoubles >= s.config.maxDoublesInRow) {
      logEvent(s, p.name + " rolled doubles " + std::to_string(p.consecutiveDoubles) +
                      " times in a row.");
      sendToRetakeExam(s, p);  // also resets consecutiveDoubles
      finishLanding(s);        // -> next player
      return true;
    }
  } else {
    p.consecutiveDoubles = 0;
  }
  moveAndResolve(s, p, steps);
  return true;
}

bool applyAction(GameState& s, const Action& a, std::mt19937& rng) {
  if (s.phase == Phase::GameOver || s.players.empty()) return false;
  Player& p = currentPlayer(s);
  if (a.playerId != p.id) return false;

  switch (s.phase) {
    case Phase::AwaitRoll:          return handleRollPhase(s, p, a, rng);
    case Phase::AwaitLandingChoice: return handleLandingChoice(s, p, a, rng);
    case Phase::AwaitCornerChoice:  return handleCornerChoice(s, p, a);
    case Phase::AwaitLiquidation:   return handleLiquidation(s, p, a);
    default:                        return false;
  }
}
