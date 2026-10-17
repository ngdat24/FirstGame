#include "GameLogic.h"

#include <algorithm>
#include <climits>
#include <string>

#include "card_data.h"

namespace {

// s = state, g = grade, msg = tin nhan, p = player, t = tiles
// ---------------------------------------------------------------------------
// Chạy log
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
    case Grade::APlus: return "A+";
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

// bán đất
void releaseTileToBank(GameState& s, Tile& t) {
  if (t.ownerId >= 0) removeOwned(s.players[t.ownerId], t.id);
  t.ownerId = -1;
  t.grade = Grade::None;
  t.totalInvested = 0;
}

// chạy trước hàm để gọi
void endGame(GameState& s);
void nextTurn(GameState& s);
void finishLanding(GameState& s);
void moveAndResolve(GameState& s, Player& p, int steps);
void settleDebt(GameState& s);
void expel(GameState& s, Player& p);
void chargeOrLiquidate(GameState& s, long long amount, int creditorId, int moveSteps);
void sendToRetakeExam(GameState& s, Player& p);

// ---------------------------------------------------------------------------
// Các lượt chơi
// ---------------------------------------------------------------------------
void endGame(GameState& s) {
  s.phase = Phase::GameOver;
  s.pendingTileId = -1;
  s.debt = PendingDebt{};
  s.winnerId = -1;
  long long bestWorth = -1;
  long long bestCash = -1;
  // So sánh kĩ từ tiền tới tên
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
    logEvent(s, "Hết 4 năm. Winner: " + s.players[s.winnerId].name +
                    " (với tổng giá trị học phần: " + std::to_string(bestWorth) + ")");
}

void nextTurn(GameState& s) {
  if (countAlivePlayers(s) <= 1) {  // check số ng còn
    endGame(s);
    return;
  }
  if (s.rentFreeTurnsLeft > 0) --s.rentFreeTurnsLeft;  // trừ bài miễn tiền

  const int n = static_cast<int>(s.players.size());
  int idx = s.currentPlayerIndex;
  for (;;) {
    bool crossedStart = false;  // did we step onto the tempo's first seat?
    do {
      idx = (idx + 1) % n;
      if (idx == s.firstPlayerIndex) crossedStart = true;  // kể cả khi ng chơi đó đã cút vẫn phải check
    } while (s.players[idx].isBankrupt);

    if (crossedStart) {  // quay 1 vòng
      if (s.currentTempo >= s.config.maxTempo) {
        s.currentPlayerIndex = idx;
        endGame(s);
        return;
      }
      ++s.currentTempo;
      logEvent(s, "--- Tempo " + std::to_string(s.currentTempo) + " begins ---");
      examSeasonPayout(s);
    }

    Player& next = s.players[idx];
    if (next.skipTurns > 0) {  // nếu có thẻ khiến ng đó bị mất lượt thì pass qua nó
      --next.skipTurns;
      if (s.rentFreeTurnsLeft > 0) --s.rentFreeTurnsLeft;
      logEvent(s, next.name + " mất lượt đi này.");
      continue;
    }
    break;
  }

  s.currentPlayerIndex = idx;
  s.phase = Phase::AwaitRoll;
  s.pendingTileId = -1;
  s.debt = PendingDebt{};
  s.players[idx].consecutiveDoubles = 0;
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
    logEvent(s, p.name + " chó đỏ.");
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
  logEvent(s, p.name + " gặp Lê Minh Quân.");
}

// ---------------------------------------------------------------------------
// 3 cơ chế chính: nợ - trả, thanh lí, cút
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
  logEvent(s, p.name + " cút");
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
  logEvent(s, p.name + " nợ " + std::to_string(amount) +
                  " và phải bỏ học phần này.");
}

// ---------------------------------------------------------------------------
// Cards
// ---------------------------------------------------------------------------
int gradeWeight(Grade g) {  // hệ số ô đất: D=1, C=2, B=3, A=6, A+=15
  switch (g) {
    case Grade::D: return 1;
    case Grade::C: return 2;
    case Grade::B: return 3;
    case Grade::A: return 6;
    case Grade::APlus: return 15;
    default: return 0;
  }
}

// Alive player with the lowest net worth, or -1. Ties are broken at random: always
// picking the lowest id would hand seat 1 free subsidies (everyone starts tied).
int poorestAliveId(GameState& s) {
  std::vector<int> tied;
  long long bestWorth = LLONG_MAX;
  for (const Player& q : s.players) {
    if (q.isBankrupt) continue;
    const long long w = calculateNetWorth(s, q);
    if (w < bestWorth) {
      bestWorth = w;
      tied.clear();
    }
    if (w == bestWorth) tied.push_back(q.id);
  }
  if (tied.empty()) return -1;
  return tied[s.deckRng() % tied.size()];
}

// Rolls are halved only when the player actually moves, so a failed roll in
// Phòng thí nghiệm VLDC or a triple-doubles jail does not waste the card.
int movementSteps(GameState& s, Player& p, int rolled) {
  if (!p.halveNextDice) return rolled;
  p.halveNextDice = false;
  const int halved = std::max(1, rolled / 2);
  logEvent(s, p.name + " bị chia đôi bước đi: " + std::to_string(rolled) + " -> " +
                  std::to_string(halved) + ".");
  return halved;
}

// bốc 1 lá bất kì. Ends with
// (hết lượt) / (trả hoặc thanh lý) / (di chuyển tới ô khác và kích hoạt nó) like any other landing.
void drawAndApplyCard(GameState& s, Player& p) {
  const int idx = drawCardIndex(s.deck, s.deckRng);
  if (idx < 0) {
    finishLanding(s);
    return;
  }
  s.deck.discardPile.push_back(idx);
  const Card& c = s.deck.cards[idx];  // cards are never resized, so this stays valid
  logEvent(s, p.name + " bốc được quả \"" + c.title + "\": " + c.description);

  switch (c.effectType) {
    case CardEffectType::MoneyFlat:
      if (c.value >= 0) {
        p.cash += c.value;
        finishLanding(s);
      } else {
        chargeOrLiquidate(s, -c.value, -1, 0);
      }
      return;

    case CardEffectType::MoneyPerProperty: {
      long long total = 0;
      for (int tileId : p.ownedTileIds) total += c.value * gradeWeight(s.tiles[tileId].grade);
      chargeOrLiquidate(s, total, -1, 0);  // a total of 0 settles instantly
      return;
    }

    case CardEffectType::PovertySubsidy: {
      const int poorest = poorestAliveId(s);
      if (poorest >= 0) {
        s.players[poorest].cash += c.value;
        logEvent(s, "Mọi người thấy " + s.players[poorest].name + " khổ quá nên tặng " +
                        std::to_string(c.value) + ".");
      }
      finishLanding(s);
      return;
    }

    case CardEffectType::GoToJail:
      sendToRetakeExam(s, p);
      finishLanding(s);  // consecutiveDoubles was reset, so the turn ends
      return;

    case CardEffectType::TeleportTile: {
      const int n = static_cast<int>(s.tiles.size());
      const int target = static_cast<int>(c.value);
      if (target < 0 || target >= n) {
        finishLanding(s);
        return;
      }
      // Forward move: wrapping past Start pays the salary, as with normal moves.
      const int steps = ((target - p.position) % n + n) % n;
      if (steps == 0) finishLanding(s);
      else moveAndResolve(s, p, steps);  // never point this at another Other tile
      return;
    }

    case CardEffectType::SkipTurn:
      p.skipTurns += 1;
      p.consecutiveDoubles = 0;  // no bonus roll either
      finishLanding(s);
      return;

    case CardEffectType::HalveNextDice:
      p.halveNextDice = true;
      finishLanding(s);
      return;

    case CardEffectType::StealBuff:
      p.stealBonusPercent = std::max(p.stealBonusPercent, static_cast<int>(c.value));
      finishLanding(s);
      return;

    case CardEffectType::RentHoliday:
      s.rentFreeTurnsLeft = countAlivePlayers(s);  // lasts until it is this player's turn again
      finishLanding(s);
      return;
  }
  finishLanding(s);
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
      logEvent(s, p.name + " Trốn vldc II.");
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
      if (s.rentFreeTurnsLeft > 0) {  // thẻ may mắn free money vẫn còn hiệu lực
        logEvent(s, p.name + " được miễn tiền học liệu tại " + t.name + " (Cúp điện).");
        finishLanding(s);
        return true;
      }
      chargeOrLiquidate(s, rentFor(s, t), t.ownerId, 0);
      return true;
    case ActionType::AttemptSteal: {
      if (!opponents || t.grade == Grade::APlus) return false;  // A+ is locked from stealing
      const long long minB = std::max(1LL, rentFor(s, t));
      if (a.amount < minB || a.amount > p.cash) return false;  // cash only, no loans
      attemptSteal(s, p, t, a.amount, rng);
      finishLanding(s);
      return true;
    }
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
      logEvent(s, p.name + " muốn học ở " + t.name + ".");
      buyTile(s, p, t);
      finishLanding(s);
      return true;
    }
    case ActionType::TeleportOwned: {
      if (a.tileId < 0 || a.tileId >= static_cast<int>(s.tiles.size())) return false;
      Tile& t = s.tiles[a.tileId];
      if (t.type != TileType::Institute || t.ownerId != p.id) return false;
      if (t.grade == Grade::None) return false;
      if (t.grade == Grade::APlus) {
        if (p.cash < fee) return false;
        p.cash -= fee;
        p.position = t.id;
        logEvent(s, p.name + " muốn đi tới " + t.name + ".");
        finishLanding(s);
        return true;
      }
      if (p.cash < fee + t.upgradeCost) return false;
      p.cash -= fee;
      p.position = t.id;
      logEvent(s, p.name + " muốn học ở " + t.name + ".");
      upgradeTile(s, p, t);
      finishLanding(s);
      return true;
    }
    default:
      return false;
  }
}

// liquidation = thanh lí
bool handleLiquidation(GameState& s, Player& p, const Action& a) {
  if (a.type != ActionType::SellTile) return false;
  if (a.tileId < 0 || a.tileId >= static_cast<int>(s.tiles.size())) return false;
  Tile& t = s.tiles[a.tileId];
  if (t.type != TileType::Institute || t.ownerId != p.id) return false;

  const long long proceeds = t.totalInvested / 2;  // 50% of total invested value
  logEvent(s, p.name + " phải bỏ học " + t.name + " và bán học phần để trả " +
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
GameState makeNewGame(int numPlayers, const std::vector<Tile>& board, unsigned seed) {
  GameState s;
  s.tiles = board;
  s.deck = makeCardDeck();
  s.deckRng.seed(seed);
  for (int i = 0; i < numPlayers; ++i) {
    Player p;
    p.id = i;
    p.name = "Thằng " + std::to_string(i + 1);
    p.cash = s.config.startCash;
    s.players.push_back(p);
  }
  if (numPlayers > 0)  // random first player (same seed -> same game)
    s.firstPlayerIndex = s.currentPlayerIndex =
        static_cast<int>(s.deckRng() % static_cast<unsigned>(numPlayers));
  return s;
}

int drawCardIndex(CardDeck& d, std::mt19937& rng) {
  if (d.cards.empty()) return -1;
  if (d.drawPile.empty()) {
    if (!d.discardPile.empty()) {
      d.drawPile.swap(d.discardPile);  // tráo bài may mắn
    } else {
      for (int i = 0; i < static_cast<int>(d.cards.size()); ++i) d.drawPile.push_back(i);
    }
    // Fisher-Yates with an explicit modulo, so the order does not depend on how
    // a particular standard library implements its distributions.
    for (int i = static_cast<int>(d.drawPile.size()) - 1; i > 0; --i) {
      const int j = static_cast<int>(rng() % static_cast<unsigned>(i + 1));
      std::swap(d.drawPile[i], d.drawPile[j]);
    }
  }
  const int idx = d.drawPile.back();
  d.drawPile.pop_back();
  return idx;
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
    logEvent(s, p.name + " đi hết 1 vòng trường quay lại Parabol và húp " +
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
  logEvent(s, p.name + " đăng kí học phần " + t.name + " với mức giá " +
                  std::to_string(t.basePrice) + ".");
  return true;
}

bool upgradeTile(GameState& s, Player& p, Tile& t) {
  if (t.type != TileType::Institute || t.ownerId != p.id) return false;
  if (t.grade == Grade::None || t.grade == Grade::APlus) return false;
  if (p.cash < t.upgradeCost) return false;
  p.cash -= t.upgradeCost;
  t.totalInvested += t.upgradeCost;
  t.grade = static_cast<Grade>(static_cast<int>(t.grade) + 1);
  logEvent(s, p.name + " nâng trình " + t.name + " lên điểm " + gradeName(t.grade) + ".");
  return true;
}

bool attemptSteal(GameState& s, Player& p, Tile& t, long long bribe, std::mt19937& rng) {
  // Success chance = bribe / total invested value of the tile, capped at 100%.
  double rate = 1.0;
  if (t.totalInvested > 0) rate = static_cast<double>(bribe) / t.totalInvested;
  if (p.stealBonusPercent > 0) {  // "Phao thi" card: one-shot bonus, used up either way
    rate += p.stealBonusPercent / 100.0;
    logEvent(s, p.name + " dùng tiền để +" + std::to_string(p.stealBonusPercent) +
                    "% lấy tỉ lệ cướp điểm.");
    p.stealBonusPercent = 0;
  }
  rate = std::min(1.0, rate);

  p.cash -= bribe;  // the bribe is spent whether or not the steal works
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  const bool success = unit(rng) < rate;  // strict '<': a 0% chance never wins

  if (success) {
    removeOwned(s.players[t.ownerId], t.id);
    t.ownerId = p.id;  // grade and totalInvested are kept
    p.ownedTileIds.push_back(t.id);
    logEvent(s, p.name + " húp " + t.name + ".");
  } else {
    if (t.ownerId >= 0 && t.ownerId < static_cast<int>(s.players.size())) {
      s.players[t.ownerId].cash += bribe;
    }
    logEvent(s, p.name + " đã cố húp " + t.name + " nma không được và mất " +
                    std::to_string(bribe) + " cho " + s.players[t.ownerId].name + ".");
  }
  return success;
}

bool cornerHasOptions(const GameState& s, const Player& p) {
  const long long avail = p.cash - s.config.adminFee;
  if (avail < 0) return false;
  for (const Tile& t : s.tiles) {
    if (t.type != TileType::Institute) continue;
    if (t.ownerId == -1 && t.basePrice <= avail) return true;
    if (t.ownerId == p.id && t.grade != Grade::None) {
      if (t.grade == Grade::APlus) return true;
      if (t.upgradeCost <= avail) return true;
    }
  }
  return false;
}

void resolveLanding(GameState& s, Player& p, Tile& t) {
  switch (t.type) {
    case TileType::AcademicWarning:
      logEvent(s, p.name + " bị áp giải đi thí nghiệm VLDC II.");
      sendToRetakeExam(s, p);
      finishLanding(s);
      return;

    case TileType::SpecialCorner:
      if (cornerHasOptions(s, p)) {
        s.phase = Phase::AwaitCornerChoice;
        s.pendingTileId = t.id;
      } else {
        logEvent(s, p.name + " thèm học nma không được học.");
        finishLanding(s);
      }
      return;

    case TileType::Institute:
      if (t.ownerId == -1) {
        if (p.cash >= t.basePrice) {
          s.phase = Phase::AwaitLandingChoice;
          s.pendingTileId = t.id;
        } else {
          logEvent(s, p.name + " không đủ tiền đăng kí " + t.name + " (" +
                          std::to_string(t.basePrice) + ").");
          finishLanding(s);  // cannot afford it: nothing to decide
        }
      } else if (t.ownerId == p.id) {
        if (t.grade != Grade::APlus && p.cash >= t.upgradeCost) {
          s.phase = Phase::AwaitLandingChoice;
          s.pendingTileId = t.id;
        } else {
          if (t.grade != Grade::APlus) {
            logEvent(s, p.name + " không đủ tiền nâng điểm " + t.name + " (" +
                            std::to_string(t.upgradeCost) + ").");
          }
          finishLanding(s);
        }
      } else {
        s.phase = Phase::AwaitLandingChoice;  // pay rent or attempt a steal
        s.pendingTileId = t.id;
      }
      return;

    case TileType::Other:  // card tile
      if (s.config.cardsEnabled && !s.deck.cards.empty()) drawAndApplyCard(s, p);
      else finishLanding(s);
      return;

    // Listed explicitly (no default:) so that adding a TileType makes the compiler point here.
    case TileType::Start:       // salary was already paid by movePlayer
    case TileType::RetakeExam:  // just visiting
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
      if (g == Grade::APlus) bonus += s.config.examBonusAPlus;
      if (g == Grade::A) bonus += s.config.examBonusA;
    }
    if (bonus > 0) {
      p.cash += bonus;
      logEvent(s, p.name + " húp hobo " + std::to_string(bonus) + ".");
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
  logEvent(s, p.name + " rolls " + std::to_string(d1) + " + " + std::to_string(d2));

  // --- In the phòng thí nghiệm vldc II: escape only with doubles (or forced fee) ----
  if (p.turnsInJail > 0) {
    if (isDouble) {
      p.turnsInJail = 0;
      p.consecutiveDoubles = 0;  // escaping with doubles gives no extra roll
      logEvent(s, p.name + " thuộc lắp mạch và về sớm.");
      moveAndResolve(s, p, movementSteps(s, p, steps));
    } else {
      --p.turnsInJail;
      if (p.turnsInJail == 0) {
        logEvent(s, p.name + " ngu mất tiền.");
        chargeOrLiquidate(s, s.config.retakeFee, -1, movementSteps(s, p, steps));  // then moves
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
      logEvent(s, p.name + " quay tay " + std::to_string(p.consecutiveDoubles) +
                      " nhiều lần.");
      sendToRetakeExam(s, p);  // also resets consecutiveDoubles
      finishLanding(s);        // -> next player
      return true;
    }
  } else {
    p.consecutiveDoubles = 0;
  }
  moveAndResolve(s, p, movementSteps(s, p, steps));
  return true;
}

LegalActions getLegalActions(const GameState& s) {
  LegalActions out;
  if (s.phase == Phase::GameOver || s.players.empty()) return out;
  const Player& p = s.players[s.currentPlayerIndex];
  auto add = [&](ActionType type, int tileId = -1) {
    Action a;
    a.type = type;
    a.playerId = p.id;
    a.tileId = tileId;
    out.actions.push_back(a);
  };

  switch (s.phase) {
    case Phase::AwaitRoll:
      add(ActionType::RollDice);
      if (p.turnsInJail > 0 && p.cash >= s.config.retakeFee) add(ActionType::PayRetakeFee);
      break;

    case Phase::AwaitLandingChoice: {
      if (s.pendingTileId < 0) break;
      const Tile& t = s.tiles[s.pendingTileId];
      if (t.ownerId == -1) {
        if (t.type == TileType::Institute && p.cash >= t.basePrice) add(ActionType::Buy);
        add(ActionType::Decline);
      } else if (t.ownerId == p.id) {
        if (t.grade != Grade::None && t.grade != Grade::APlus && p.cash >= t.upgradeCost)
          add(ActionType::Upgrade);
        add(ActionType::Decline);
      } else {  // somebody else's tile: pay rent or try to steal (unless locked at A+)
        add(ActionType::PayRent);
        const long long minB = std::max(1LL, rentFor(s, t));
        if (t.grade != Grade::APlus && p.cash >= minB) {
          add(ActionType::AttemptSteal);
          out.minBribe = minB;
          out.maxBribe = p.cash;
        }
      }
      break;
    }

    case Phase::AwaitCornerChoice: {
      add(ActionType::Decline);
      const long long fee = s.config.adminFee;
      for (const Tile& t : s.tiles) {
        if (t.type != TileType::Institute) continue;
        if (t.ownerId == -1 && p.cash >= fee + t.basePrice)
          add(ActionType::TeleportUnowned, t.id);
        else if (t.ownerId == p.id && t.grade != Grade::None) {
          if (t.grade == Grade::APlus && p.cash >= fee)
            add(ActionType::TeleportOwned, t.id);
          else if (t.grade != Grade::APlus && p.cash >= fee + t.upgradeCost)
            add(ActionType::TeleportOwned, t.id);
        }
      }
      break;
    }

    case Phase::AwaitLiquidation:
      for (int tileId : p.ownedTileIds) add(ActionType::SellTile, tileId);
      break;

    default:
      break;
  }
  return out;
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
