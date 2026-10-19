// Scripted rule tests. Dice are injected with applyRoll() so every scenario is
// exact. Build & run:
//   g++ -std=c++17 -Wall -Wextra -O2 tests.cpp GameLogic.cpp -o tests && ./tests
#include <iostream>
#include <random>

#include "GameLogic.h"
#include "board_data.h"

static int g_total = 0;
static int g_failed = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    ++g_total;                                                           \
    if (!(cond)) {                                                       \
      ++g_failed;                                                        \
      std::cout << "FAIL (line " << __LINE__ << "): " #cond << "\n";     \
    }                                                                    \
  } while (0)

static Action act(ActionType type, int playerId, long long amount = 0, int tileId = -1,
                  Grade target = Grade::None) {
  Action a;
  a.type = type;
  a.playerId = playerId;
  a.amount = amount;
  a.tileId = tileId;
  a.targetGrade = target;
  return a;
}

// makeNewGame() starts at a random seat; tests want player 0 first so scenarios are fixed.
static GameState fresh() {
  GameConfig config;
  config.startCashOrderStep = 0;  // scripted scenarios use equal cash and force seat 0
  GameState s = makeNewGame(4, makeBoard(), 1, config);
  s.firstPlayerIndex = s.currentPlayerIndex = 0;
  return s;
}

// Give `ownerId` a tile at the given grade with the given invested value.
static void giveTile(GameState& s, int tileId, int ownerId, Grade g, int invested) {
  Tile& t = s.tiles[tileId];
  t.ownerId = ownerId;
  t.grade = g;
  t.totalInvested = invested;
  s.players[ownerId].ownedTileIds.push_back(tileId);
}

static void testDoublesAndTripleDoublesJail() {
  GameState s = fresh();
  Player& p0 = s.players[0];

  CHECK(applyRoll(s, 2, 2));  // doubles: moves 4 -> tile 4 (institute)
  CHECK(p0.position == 4);
  CHECK(s.phase == Phase::AwaitLandingChoice);
  std::mt19937 rng(1);
  CHECK(applyAction(s, act(ActionType::Decline, 0), rng));
  CHECK(s.currentPlayerIndex == 0);  // doubles: same player rolls again
  CHECK(s.phase == Phase::AwaitRoll);
  CHECK(p0.consecutiveDoubles == 1);

  CHECK(applyRoll(s, 3, 3));  // doubles again: 4 + 6 = tile 10 (jail, just visiting)
  CHECK(p0.position == 10);
  CHECK(s.currentPlayerIndex == 0 && s.phase == Phase::AwaitRoll);
  CHECK(p0.turnsInJail == 0);

  CHECK(applyRoll(s, 1, 1));  // third double in the same turn -> jail, turn ends
  CHECK(p0.turnsInJail == 3);
  CHECK(p0.position == 10);
  CHECK(p0.consecutiveDoubles == 0);
  CHECK(s.currentPlayerIndex == 1);
  CHECK(s.phase == Phase::AwaitRoll);
}

static void testNonDoubleResetsStreakAndBuy() {
  GameState s = fresh();
  std::mt19937 rng(1);
  CHECK(applyRoll(s, 1, 1));  // double -> tile 2
  CHECK(applyAction(s, act(ActionType::Decline, 0), rng));
  CHECK(s.players[0].consecutiveDoubles == 1);
  CHECK(applyRoll(s, 1, 2));  // non-double: streak resets, moves to tile 5 (Other)
  CHECK(s.players[0].consecutiveDoubles == 0);
  CHECK(applyAction(s, act(ActionType::Decline, 0), rng));
  CHECK(s.currentPlayerIndex == 1);

  GameState b = fresh();
  CHECK(applyRoll(b, 1, 2));  // tile 3 = "Mechanical Workshop"
  CHECK(b.phase == Phase::AwaitLandingChoice);
  const long long price = b.tiles[3].basePrice;
  CHECK(applyAction(b, act(ActionType::Buy, 0), rng));
  CHECK(b.tiles[3].ownerId == 0 && b.tiles[3].grade == Grade::D);
  CHECK(b.players[0].cash == b.config.startCash - price);
  CHECK(b.currentPlayerIndex == 1);
}

static void testPassingStartPaysSalaryOnly() {
  GameState s = fresh();
  s.players[0].position = 38;
  const long long before = s.players[0].cash;
  CHECK(applyRoll(s, 1, 3));  // 38 + 4 = 42 -> tile 2, passes Start
  CHECK(s.players[0].cash == before + s.config.salary);
  // No upgrade right is granted for passing Start: tile 2 is unowned -> buy prompt only.
  CHECK(s.phase == Phase::AwaitLandingChoice);
}

static void testRentAndLiquidation() {
  GameState s = fresh();
  std::mt19937 rng(1);
  giveTile(s, 2, 0, Grade::A, 300000);
  const long long rent = rentFor(s, s.tiles[2]);  // baseRent 11000 x the grade-A factor
  CHECK(rent == 11000LL * s.config.rentFactor[4]);
  giveTile(s, 3, 1, Grade::B, 2 * rent);  // sells for exactly `rent`
  const long long proceeds = s.tiles[3].totalInvested / 2;
  CHECK(proceeds == rent);
  s.players[1].cash = 1000;
  s.currentPlayerIndex = 1;
  const long long ownerBefore = s.players[0].cash;

  CHECK(applyRoll(s, 1, 1));  // player 1 lands on tile 2
  CHECK(s.phase == Phase::AwaitLandingChoice);
  CHECK(!applyAction(s, act(ActionType::Decline, 1), rng));  // cannot ignore rent
  CHECK(applyAction(s, act(ActionType::PayRent, 1), rng));
  CHECK(s.phase == Phase::AwaitLiquidation);
  CHECK(!applyAction(s, act(ActionType::RollDice, 1), rng));  // must sell first
  CHECK(!applyAction(s, act(ActionType::SellTile, 1, 0, 2), rng));  // not their tile
  CHECK(applyAction(s, act(ActionType::SellTile, 1, 0, 3), rng));

  CHECK(s.tiles[3].ownerId == -1 && s.tiles[3].grade == Grade::None &&
        s.tiles[3].totalInvested == 0);
  CHECK(s.players[1].ownedTileIds.empty());
  CHECK(s.players[1].cash == 1000 + proceeds - rent);
  CHECK(s.players[0].cash == ownerBefore + rent);
  CHECK(!s.players[1].isBankrupt);
  CHECK(s.currentPlayerIndex == 1 && s.phase == Phase::AwaitRoll);  // doubles: rolls again
}

static void testExpulsion() {
  GameState s = fresh();
  std::mt19937 rng(1);
  giveTile(s, 2, 0, Grade::A, 300000);  // rent 88000
  s.players[1].cash = 1000;             // owns nothing
  s.currentPlayerIndex = 1;
  const long long ownerBefore = s.players[0].cash;

  CHECK(applyRoll(s, 1, 1));
  CHECK(applyAction(s, act(ActionType::PayRent, 1), rng));
  CHECK(s.players[1].isBankrupt);
  CHECK(s.players[1].cash == 0);
  CHECK(s.players[0].cash == ownerBefore + 1000);  // leftover cash goes to creditor
  CHECK(countAlivePlayers(s) == 3);
  CHECK(s.currentPlayerIndex == 2 && s.phase == Phase::AwaitRoll);
}

static void testStealSuccessKeepsGrade() {
  GameState s = fresh();
  std::mt19937 rng(1);
  giveTile(s, 2, 0, Grade::B, 150000);
  s.currentPlayerIndex = 1;
  CHECK(applyRoll(s, 1, 1));
  const long long fullB = (3LL * s.tiles[2].totalInvested + 1) / 2;  // 1.5x value = 100% chance
  CHECK(applyAction(s, act(ActionType::AttemptSteal, 1, fullB), rng));
  CHECK(s.tiles[2].ownerId == 1);
  CHECK(s.tiles[2].grade == Grade::B);
  CHECK(s.tiles[2].totalInvested == 150000);
  CHECK(s.players[1].cash == s.config.startCash - fullB);
  CHECK(s.players[0].ownedTileIds.empty());
  CHECK(s.players[1].ownedTileIds.size() == 1);
}

static void testStealFailureSinglePenaltyAndValidation() {
  GameState s = fresh();
  std::mt19937 rng(1);
  giveTile(s, 2, 0, Grade::B, 150000);
  s.currentPlayerIndex = 1;
  CHECK(applyRoll(s, 1, 1));
  const long long ownerCash = s.players[0].cash;

  const long long minB = (3LL * rentFor(s, s.tiles[2]) + 1) / 2;
  const long long fullB = (3LL * s.tiles[2].totalInvested + 1) / 2;
  CHECK(!applyAction(s, act(ActionType::AttemptSteal, 1, 0), rng));          // bribe must be >= 1.5x rent
  CHECK(!applyAction(s, act(ActionType::AttemptSteal, 1, minB - 1), rng));   // just below 1.5x rent
  CHECK(!applyAction(s, act(ActionType::AttemptSteal, 1, fullB + 1), rng));  // above 1.5x value
  CHECK(!applyAction(s, act(ActionType::AttemptSteal, 1, -5), rng));         // negative
  CHECK(!applyAction(s, act(ActionType::AttemptSteal, 1, 2000000), rng));    // more than cash
  CHECK(s.phase == Phase::AwaitLandingChoice);                               // nothing changed

  CHECK(applyAction(s, act(ActionType::AttemptSteal, 1, minB), rng));        // 1.5x rent = 0% chance
  CHECK(s.tiles[2].ownerId == 0);                      // steal failed
  CHECK(s.players[1].cash == s.config.startCash - minB);  // lost the bribe
  CHECK(s.players[0].cash == ownerCash + minB);        // failed bribe paid to owner

  // A+ tiles are locked and cannot be stolen.
  GameState locked = fresh();
  giveTile(locked, 2, 0, Grade::APlus, 350000);
  locked.currentPlayerIndex = 1;
  CHECK(applyRoll(locked, 1, 1));
  CHECK(!applyAction(locked, act(ActionType::AttemptSteal, 1, 500000), rng));
}

static void testJailRules() {
  std::mt19937 rng(1);

  // Escape with doubles: moves, but no extra roll.
  GameState a = fresh();
  a.currentPlayerIndex = 1;
  a.players[1].position = 10;
  a.players[1].turnsInJail = 3;
  CHECK(applyRoll(a, 2, 2));
  CHECK(a.players[1].turnsInJail == 0);
  CHECK(a.players[1].position == 14);
  CHECK(applyAction(a, act(ActionType::Decline, 1), rng));
  CHECK(a.currentPlayerIndex == 2);

  // Failing a roll in jail: lose the turn, counter goes down.
  GameState b = fresh();
  b.currentPlayerIndex = 1;
  b.players[1].position = 10;
  b.players[1].turnsInJail = 3;
  CHECK(applyRoll(b, 1, 2));
  CHECK(b.players[1].turnsInJail == 2);
  CHECK(b.players[1].position == 10);
  CHECK(b.currentPlayerIndex == 2);

  // Third failed roll: forced fee, then moves by that roll.
  GameState c = fresh();
  c.currentPlayerIndex = 1;
  c.players[1].position = 10;
  c.players[1].turnsInJail = 1;
  CHECK(applyRoll(c, 1, 2));
  CHECK(c.players[1].turnsInJail == 0);
  CHECK(c.players[1].cash == c.config.startCash - c.config.retakeFee);
  CHECK(c.players[1].position == 13);

  // Voluntary fee: pay, then roll normally.
  GameState d = fresh();
  d.players[0].position = 10;
  d.players[0].turnsInJail = 2;
  CHECK(applyAction(d, act(ActionType::PayRetakeFee, 0), rng));
  CHECK(d.players[0].turnsInJail == 0);
  CHECK(d.players[0].cash == d.config.startCash - d.config.retakeFee);
  CHECK(d.currentPlayerIndex == 0 && d.phase == Phase::AwaitRoll);
  CHECK(!applyAction(d, act(ActionType::PayRetakeFee, 0), rng));  // not in jail anymore
}

static void testAcademicWarning() {
  GameState s = fresh();
  s.players[0].position = 27;
  CHECK(applyRoll(s, 1, 2));  // 27 + 3 = 30 = Academic Warning
  CHECK(s.players[0].position == 10);
  CHECK(s.players[0].turnsInJail == 3);
  CHECK(s.currentPlayerIndex == 1);

  GameState t = fresh();
  t.players[0].position = 28;
  CHECK(applyRoll(t, 1, 1));  // doubles onto Academic Warning: still no extra roll
  CHECK(t.players[0].turnsInJail == 3);
  CHECK(t.currentPlayerIndex == 1);
}

static void testSpecialCorner() {
  std::mt19937 rng(1);

  // Teleport to an unowned institute: moves, pays fee + price, no salary.
  GameState s = fresh();
  s.players[0].position = 20;
  resolveLanding(s, s.players[0], s.tiles[20]);
  CHECK(s.phase == Phase::AwaitCornerChoice);
  const long long start = s.players[0].cash;
  CHECK(!applyAction(s, act(ActionType::TeleportUnowned, 0, 0, 25), rng));  // not an institute
  CHECK(applyAction(s, act(ActionType::TeleportUnowned, 0, 0, 26), rng));
  CHECK(s.players[0].position == 26);
  CHECK(s.tiles[26].ownerId == 0 && s.tiles[26].grade == Grade::D);
  CHECK(s.players[0].cash == start - s.config.adminFee - s.tiles[26].basePrice);
  CHECK(s.currentPlayerIndex == 1);

  // Teleport to an owned institute: upgrades it (up to A+), or just moves if already A+.
  GameState u = fresh();
  giveTile(u, 7, 0, Grade::D, u.tiles[7].basePrice);
  u.players[0].position = 20;
  resolveLanding(u, u.players[0], u.tiles[20]);
  CHECK(applyAction(u, act(ActionType::TeleportOwned, 0, 0, 7), rng));
  CHECK(u.tiles[7].grade == Grade::C);
  CHECK(u.players[0].position == 7);

  GameState ap = fresh();
  giveTile(ap, 7, 0, Grade::APlus, 350000);
  ap.players[0].position = 20;
  const long long beforeAp = ap.players[0].cash;
  resolveLanding(ap, ap.players[0], ap.tiles[20]);
  CHECK(applyAction(ap, act(ActionType::TeleportOwned, 0, 0, 7), rng));
  CHECK(ap.tiles[7].grade == Grade::APlus);
  CHECK(ap.players[0].position == 7);
  CHECK(ap.players[0].cash == beforeAp - ap.config.adminFee);

  // Nothing affordable: the tile is a no-op and the turn simply ends.
  GameState n = fresh();
  n.players[0].cash = 0;
  n.players[0].position = 20;
  resolveLanding(n, n.players[0], n.tiles[20]);
  CHECK(n.phase == Phase::AwaitRoll);
  CHECK(n.currentPlayerIndex == 1);
  CHECK(n.players[0].position == 20);
}

static void testExamSeason() {
  GameState s = fresh();
  giveTile(s, 2, 0, Grade::APlus, 350000);
  giveTile(s, 3, 0, Grade::A, 300000);
  giveTile(s, 6, 0, Grade::B, 200000);  // B, C and D earn nothing
  const long long base = s.players[0].cash;
  const long long bonus = s.config.examBonusA + s.config.examBonusAPlus;

  s.currentTempo = 5;
  examSeasonPayout(s);
  CHECK(s.players[0].cash == base);
  s.currentTempo = 6;
  examSeasonPayout(s);
  CHECK(s.players[0].cash == base + bonus);
  s.currentTempo = 12;
  examSeasonPayout(s);
  CHECK(s.players[0].cash == base + 2 * bonus);
  s.currentTempo = 13;  // first tempo of the next semester
  examSeasonPayout(s);
  CHECK(s.players[0].cash == base + 2 * bonus);
  s.currentTempo = 18;  // exam tempo of the third semester
  examSeasonPayout(s);
  CHECK(s.players[0].cash == base + 3 * bonus);
}

static void testGameEndAtMaxTempo() {
  GameState s = fresh();
  std::mt19937 rng(1);
  s.currentTempo = s.config.maxTempo;
  s.currentPlayerIndex = 3;
  s.players[2].cash = 5000000;  // richest
  CHECK(applyRoll(s, 1, 2));
  CHECK(applyAction(s, act(ActionType::Decline, 3), rng));  // last player's turn ends
  CHECK(s.phase == Phase::GameOver);
  CHECK(s.winnerId == 2);
  CHECK(!applyAction(s, act(ActionType::RollDice, 0), rng));  // nothing works afterwards
}

static void testTempoAdvancesAfterEveryone() {
  GameState s = fresh();
  std::mt19937 rng(1);
  CHECK(s.currentTempo == 1);
  for (int i = 0; i < 4; ++i) {
    CHECK(applyRoll(s, 1, 2));  // non-double: lands on tile 3 first time, others pay rent/decline
    while (s.phase == Phase::AwaitLandingChoice) {
      const Tile& t = s.tiles[s.pendingTileId];
      const ActionType type = (t.ownerId == -1) ? ActionType::Decline
                              : (t.ownerId == s.currentPlayerIndex) ? ActionType::Decline
                                                                    : ActionType::PayRent;
      CHECK(applyAction(s, act(type, s.currentPlayerIndex), rng));
    }
  }
  CHECK(s.currentTempo == 2);
  CHECK(s.currentPlayerIndex == 0);
}

static void testIllegalActionsRejected() {
  GameState s = fresh();
  std::mt19937 rng(1);
  CHECK(!applyAction(s, act(ActionType::Buy, 0), rng));       // wrong phase
  CHECK(!applyAction(s, act(ActionType::RollDice, 2), rng));  // not their turn
  CHECK(!applyAction(s, act(ActionType::SellTile, 0, 0, 3), rng));
  CHECK(!applyRoll(s, 0, 3));                                  // invalid die
  CHECK(!applyRoll(s, 7, 1));
  CHECK(s.currentPlayerIndex == 0 && s.phase == Phase::AwaitRoll);
}


// ===========================================================================
// Card system
// ===========================================================================
static int findCard(const GameState& s, CardEffectType type) {
  for (int i = 0; i < static_cast<int>(s.deck.cards.size()); ++i)
    if (s.deck.cards[i].effectType == type) return i;
  return -1;
}

// Make `idx` the next card drawn.
static void stackDeck(GameState& s, int idx) {
  s.deck.drawPile.assign(1, idx);
  s.deck.discardPile.clear();
}

// Add a custom card to the catalogue and make it the next draw.
static void stackCustom(GameState& s, CardEffectType type, long long value) {
  Card c;
  c.id = 99;
  c.title = "test";
  c.effectType = type;
  c.value = value;
  s.deck.cards.push_back(c);
  stackDeck(s, static_cast<int>(s.deck.cards.size()) - 1);
}

// Current player rolls onto card tile 15.
static void landOnCardTile(GameState& s) {
  s.players[s.currentPlayerIndex].position = 10;
  CHECK(applyRoll(s, 2, 3));
}

// One uneventful turn: roll 1+2 (an unowned institute) and decline.
static void playQuietTurn(GameState& s, std::mt19937& rng) {
  const int who = s.currentPlayerIndex;
  s.players[who].position = 0;
  CHECK(applyRoll(s, 1, 2));
  CHECK(s.phase == Phase::AwaitLandingChoice);
  CHECK(applyAction(s, act(ActionType::Decline, who), rng));
}

static void testDeckContentAndCycle() {
  GameState s = fresh();
  const int n = static_cast<int>(s.deck.cards.size());
  CHECK(n == 11);
  CHECK(s.deck.drawPile.empty() && s.deck.discardPile.empty());
  for (int i = 0; i < n; ++i) CHECK(s.deck.cards[i].id == i + 1);

  // Every card comes out exactly once per cycle, then the discards are reshuffled.
  std::vector<int> seen(n, 0);
  for (int i = 0; i < n; ++i) {
    const int idx = drawCardIndex(s.deck, s.deckRng);
    CHECK(idx >= 0 && idx < n);
    if (idx >= 0 && idx < n) ++seen[idx];
    s.deck.discardPile.push_back(idx);
  }
  for (int i = 0; i < n; ++i) CHECK(seen[i] == 1);
  CHECK(s.deck.drawPile.empty());
  const int again = drawCardIndex(s.deck, s.deckRng);
  CHECK(again >= 0 && again < n);
  CHECK(static_cast<int>(s.deck.drawPile.size()) == n - 1);
  CHECK(s.deck.discardPile.empty());

  // Same seed -> same shuffle; empty deck -> -1.
  GameState a = makeNewGame(4, makeBoard(), 7), b = makeNewGame(4, makeBoard(), 7);
  CHECK(drawCardIndex(a.deck, a.deckRng) == drawCardIndex(b.deck, b.deckRng));
  CardDeck empty;
  CHECK(drawCardIndex(empty, a.deckRng) == -1);
}

static void testCardMoney() {
  std::mt19937 rng(1);
  // Pay: -150k.
  GameState a = fresh();
  stackDeck(a, 7);  // "Đóng tiền học phí kỳ phụ" is card #8 -> index 7
  CHECK(a.deck.cards[7].value == -150000);
  landOnCardTile(a);
  CHECK(a.players[0].cash == a.config.startCash - 150000);
  CHECK(a.currentPlayerIndex == 1);
  CHECK(a.deck.discardPile.size() == 1 && a.deck.drawPile.empty());

  // Gain: +100k.
  GameState g = fresh();
  stackDeck(g, 10);
  CHECK(g.deck.cards[10].value == 100000);
  landOnCardTile(g);
  CHECK(g.players[0].cash == g.config.startCash + 100000);

  // A fine you cannot cover forces liquidation.
  GameState l = fresh();
  stackDeck(l, 7);
  giveTile(l, 2, 0, Grade::B, 400000);  // sells for 200000
  l.players[0].cash = 10000;
  landOnCardTile(l);
  CHECK(l.phase == Phase::AwaitLiquidation);
  CHECK(applyAction(l, act(ActionType::SellTile, 0, 0, 2), rng));
  CHECK(l.players[0].cash == 10000 + 200000 - 150000);
  CHECK(l.currentPlayerIndex == 1);
}

static void testCardPerProperty() {
  GameState s = fresh();
  stackDeck(s, findCard(s, CardEffectType::MoneyPerProperty));
  giveTile(s, 2, 0, Grade::A, 300000);  // weight 6
  giveTile(s, 3, 0, Grade::D, 120000);  // weight 1
  landOnCardTile(s);
  CHECK(s.players[0].cash == s.config.startCash - 10000LL * (6 + 1));

  GameState poor = fresh();  // owns nothing -> pays nothing
  stackDeck(poor, findCard(poor, CardEffectType::MoneyPerProperty));
  landOnCardTile(poor);
  CHECK(poor.players[0].cash == poor.config.startCash);
  CHECK(poor.currentPlayerIndex == 1);
}

static void testCardPovertySubsidy() {
  GameState s = fresh();
  stackDeck(s, findCard(s, CardEffectType::PovertySubsidy));
  s.players[2].cash = 100;  // poorest by far
  landOnCardTile(s);        // player 0 draws it...
  CHECK(s.players[2].cash == 100 + 300000);  // ...but player 2 gets the money
  CHECK(s.players[0].cash == s.config.startCash);
}

static void testSubsidyTieIsRandom() {
  // Everyone starts with the same net worth, so all four are tied as "poorest".
  int received[4] = {0, 0, 0, 0};
  for (unsigned seed = 1; seed <= 200; ++seed) {
    GameConfig config;
    config.startCashOrderStep = 0;
    GameState s = makeNewGame(4, makeBoard(), seed, config);
    s.firstPlayerIndex = s.currentPlayerIndex = 0;
    stackDeck(s, findCard(s, CardEffectType::PovertySubsidy));
    landOnCardTile(s);
    int winners = 0;
    for (int i = 0; i < 4; ++i)
      if (s.players[i].cash == s.config.startCash + 300000) {
        ++received[i];
        ++winners;
      }
    CHECK(winners == 1);  // exactly one player gets it
  }
  for (int i = 0; i < 4; ++i) CHECK(received[i] > 20);  // not always the lowest id
}

static void testCardJailAndTeleport() {
  GameState j = fresh();
  stackDeck(j, findCard(j, CardEffectType::GoToJail));
  landOnCardTile(j);
  CHECK(j.players[0].position == 10 && j.players[0].turnsInJail == 3);
  CHECK(j.currentPlayerIndex == 1);

  // Teleport to Start from tile 5: wraps past Start -> salary.
  GameState t = fresh();
  stackCustom(t, CardEffectType::TeleportTile, 0);
  landOnCardTile(t);
  CHECK(t.players[0].position == 0);
  CHECK(t.players[0].cash == t.config.startCash + t.config.salary);

  // Teleport onto the Special Corner: its choice phase starts.
  GameState c = fresh();
  stackCustom(c, CardEffectType::TeleportTile, 20);
  landOnCardTile(c);
  CHECK(c.players[0].position == 20);
  CHECK(c.phase == Phase::AwaitCornerChoice);
}

static void testCardSkipTurn() {
  GameState s = fresh();
  std::mt19937 rng(1);
  stackDeck(s, findCard(s, CardEffectType::SkipTurn));
  landOnCardTile(s);
  CHECK(s.players[0].skipTurns == 1);
  CHECK(s.currentPlayerIndex == 1);

  playQuietTurn(s, rng);  // player 1
  playQuietTurn(s, rng);  // player 2
  playQuietTurn(s, rng);  // player 3
  CHECK(s.currentTempo == 2);           // the round still counted
  CHECK(s.currentPlayerIndex == 1);     // player 0 was skipped
  CHECK(s.players[0].skipTurns == 0);   // and the penalty is used up
  playQuietTurn(s, rng);                // 1
  playQuietTurn(s, rng);                // 2
  playQuietTurn(s, rng);                // 3
  CHECK(s.currentPlayerIndex == 0);     // player 0 plays normally again

  // Drawing it on a doubles roll also cancels the bonus roll.
  GameState d = fresh();
  stackDeck(d, findCard(d, CardEffectType::SkipTurn));
  d.players[0].position = 11;
  CHECK(applyRoll(d, 2, 2));  // doubles: 1 + 4 = tile 5, a card tile
  CHECK(d.players[0].skipTurns == 1);
  CHECK(d.players[0].consecutiveDoubles == 0);
  CHECK(d.currentPlayerIndex == 1);  // turn passes instead of rolling again
}

static void testCardHalveDice() {
  // 11 -> 5, 12 -> 6 (rounded down), flag is used up by the move.
  GameState a = fresh();
  a.players[0].halveNextDice = true;
  a.players[0].position = 1;
  CHECK(applyRoll(a, 5, 6));
  CHECK(a.players[0].position == 6);
  CHECK(!a.players[0].halveNextDice);

  GameState b = fresh();
  b.players[0].halveNextDice = true;
  b.players[0].position = 1;
  CHECK(applyRoll(b, 6, 6));  // doubles still count as doubles
  CHECK(b.players[0].position == 7);
  CHECK(b.players[0].consecutiveDoubles == 1);

  // A failed roll in jail does not waste it.
  GameState j = fresh();
  j.players[0].halveNextDice = true;
  j.players[0].position = 10;
  j.players[0].turnsInJail = 3;
  CHECK(applyRoll(j, 1, 2));
  CHECK(j.players[0].halveNextDice);

  // Via the card itself.
  GameState c = fresh();
  stackDeck(c, findCard(c, CardEffectType::HalveNextDice));
  landOnCardTile(c);
  CHECK(c.players[0].halveNextDice);
}

static void testCardStealBuff() {
  std::mt19937 rng(1);
  GameState s = fresh();
  stackDeck(s, findCard(s, CardEffectType::StealBuff));
  landOnCardTile(s);
  CHECK(s.players[0].stealBonusPercent == 25);

  // A 100% bonus makes even a minimum-bribe steal certain, and the bonus is consumed.
  GameState t = fresh();
  giveTile(t, 2, 0, Grade::B, 150000);
  t.currentPlayerIndex = 1;
  t.players[1].stealBonusPercent = 100;
  CHECK(applyRoll(t, 1, 1));
  const long long minBt = (3LL * rentFor(t, t.tiles[2]) + 1) / 2;
  CHECK(applyAction(t, act(ActionType::AttemptSteal, 1, minBt), rng));
  CHECK(t.tiles[2].ownerId == 1);
  CHECK(t.players[1].stealBonusPercent == 0);

  // A failed attempt also uses it up.
  GameState u = fresh();
  giveTile(u, 2, 0, Grade::D, 150000);
  u.currentPlayerIndex = 1;
  u.players[1].stealBonusPercent = 1;
  CHECK(applyRoll(u, 1, 1));
  const long long minBu = (3LL * rentFor(u, u.tiles[2]) + 1) / 2;
  CHECK(applyAction(u, act(ActionType::AttemptSteal, 1, minBu), rng));
  CHECK(u.players[1].stealBonusPercent == 0);
}

static void testStealBonusExpires() {
  GameState s = fresh();
  std::mt19937 rng(1);
  stackDeck(s, findCard(s, CardEffectType::StealBuff));
  landOnCardTile(s);  // player 0 draws it during tempo 1
  CHECK(s.players[0].stealBonusPercent == 25);
  CHECK(s.players[0].stealBonusUntilTempo == 1 + s.config.stealBonusTempos - 1);  // tempos 1, 2, 3

  for (int i = 0; i < 3; ++i) playQuietTurn(s, rng);  // players 1-3 finish tempo 1
  CHECK(s.currentTempo == 2);
  CHECK(s.players[0].stealBonusPercent == 25);  // still valid in tempo 2
  for (int i = 0; i < 4; ++i) playQuietTurn(s, rng);
  CHECK(s.currentTempo == 3);
  CHECK(s.players[0].stealBonusPercent == 25);  // ...and in tempo 3 (the last valid one)
  for (int i = 0; i < 4; ++i) playQuietTurn(s, rng);
  CHECK(s.currentTempo == 4);
  CHECK(s.players[0].stealBonusPercent == 0);   // gone when tempo 4 begins
  CHECK(s.players[0].stealBonusUntilTempo == 0);

  // Drawing the card again refreshes the timer instead of stacking the percentage.
  GameState r = fresh();
  r.currentTempo = 5;
  stackDeck(r, findCard(r, CardEffectType::StealBuff));
  landOnCardTile(r);
  CHECK(r.players[0].stealBonusUntilTempo == 7);
  r.currentPlayerIndex = 0;
  r.phase = Phase::AwaitRoll;
  r.currentTempo = 6;
  stackDeck(r, findCard(r, CardEffectType::StealBuff));
  landOnCardTile(r);
  CHECK(r.players[0].stealBonusPercent == 25);
  CHECK(r.players[0].stealBonusUntilTempo == 8);

  // A bonus that was already used up does not come back.
  GameState u = fresh();
  u.players[1].stealBonusPercent = 0;
  u.players[1].stealBonusUntilTempo = 9;
  for (int i = 0; i < 4; ++i) playQuietTurn(u, rng);
  CHECK(u.players[1].stealBonusPercent == 0);
}

static void testCardRentHoliday() {
  std::mt19937 rng(1);
  GameState s = fresh();
  giveTile(s, 2, 3, Grade::A, 300000);  // player 3 would collect big rent
  stackDeck(s, findCard(s, CardEffectType::RentHoliday));
  landOnCardTile(s);
  CHECK(s.rentFreeTurnsLeft == 3);  // 4 players alive; one tick already used
  CHECK(s.currentPlayerIndex == 1);

  const long long before = s.players[1].cash;
  s.players[1].position = 0;
  CHECK(applyRoll(s, 1, 1));  // player 1 lands on player 3's tile
  CHECK(applyAction(s, act(ActionType::PayRent, 1), rng));
  CHECK(s.players[1].cash == before);  // no rent this round

  // After a full round the holiday is over.
  GameState t = fresh();
  stackDeck(t, findCard(t, CardEffectType::RentHoliday));
  landOnCardTile(t);
  playQuietTurn(t, rng);
  playQuietTurn(t, rng);
  playQuietTurn(t, rng);
  CHECK(t.currentPlayerIndex == 0);
  CHECK(t.rentFreeTurnsLeft == 0);
}

static void testCardsSwitchAndRegularTiles() {
  GameState s = fresh();
  s.config.cardsEnabled = false;
  landOnCardTile(s);
  CHECK(s.deck.discardPile.empty());  // nothing drawn
  CHECK(s.currentPlayerIndex == 1);
  CHECK(s.players[0].cash == s.config.startCash);
}

static void testStartingCashAndRentScaling() {
  const long long expected[] = {1000000, 1000000, 1000000, 1000000};
  for (unsigned seed = 1; seed <= 40; ++seed) {
    GameState s = makeNewGame(4, makeBoard(), seed);
    long long total = 0;
    for (int order = 0; order < 4; ++order) {
      const Player& p = s.players[(s.firstPlayerIndex + order) % 4];
      CHECK(p.cash == expected[order]);
      total += p.cash;
    }
    CHECK(total == 4 * s.config.startCash);
  }
  GameConfig custom;
  custom.startCash = 1200000;
  custom.startCashOrderStep = 20000;
  GameState s = makeNewGame(4, makeBoard(), 7, custom);
  CHECK(s.players[s.firstPlayerIndex].cash == 1170000);
  CHECK(s.players[(s.firstPlayerIndex + 3) % 4].cash == 1230000);
  custom.startCashOrderStep = 0;
  for (const Player& p : makeNewGame(4, makeBoard(), 7, custom).players)
    CHECK(p.cash == 1200000);

  s = fresh();
  giveTile(s, 1, 1, Grade::D, 100000);
  s.config.rentPercent = 80;
  CHECK(rentFor(s, s.tiles[1]) == 24000);  // factor stays 3, not truncated to 2
  CHECK(s.config.rentFactor[1] == 3);
  s.players[0].position = 38;
  CHECK(applyRoll(s, 1, 2));  // lands on tile 1 owned by player 1
  CHECK(getLegalActions(s).minBribe == 36000);
  std::mt19937 rng(1);
  const long long ownerCash = s.players[1].cash;
  CHECK(applyAction(s, act(ActionType::PayRent, 0), rng));
  CHECK(s.players[1].cash == ownerCash + 24000);
  s.config.rentPercent = 33;
  CHECK(rentFor(s, s.tiles[1]) == 9900);
  s.config.rentPercent = 0;
  CHECK(rentFor(s, s.tiles[1]) == 0);
}

static void testRandomFirstPlayer() {
  int seen[4] = {0, 0, 0, 0};
  for (unsigned seed = 1; seed <= 200; ++seed) {
    GameState s = makeNewGame(4, makeBoard(), seed);
    CHECK(s.firstPlayerIndex >= 0 && s.firstPlayerIndex < 4);
    CHECK(s.currentPlayerIndex == s.firstPlayerIndex);
    if (s.firstPlayerIndex >= 0 && s.firstPlayerIndex < 4) ++seen[s.firstPlayerIndex];
    GameState t = makeNewGame(4, makeBoard(), seed);
    CHECK(t.firstPlayerIndex == s.firstPlayerIndex);  // same seed -> same start
  }
  for (int i = 0; i < 4; ++i) CHECK(seen[i] > 20);    // every seat starts about 1/4 of the time
}

static void testTempoBoundaryFollowsFirstPlayer() {
  std::mt19937 rng(1);
  GameState s = fresh();
  s.firstPlayerIndex = s.currentPlayerIndex = 2;
  playQuietTurn(s, rng);  // player 2
  playQuietTurn(s, rng);  // player 3
  CHECK(s.currentPlayerIndex == 0);
  CHECK(s.currentTempo == 1);  // running off the end of the array is NOT a new tempo
  playQuietTurn(s, rng);  // player 0
  playQuietTurn(s, rng);  // player 1
  CHECK(s.currentPlayerIndex == 2);
  CHECK(s.currentTempo == 2);  // back at the first seat: tempo 2 begins

  // If the first seat's player has been expelled, the boundary is still counted once.
  GameState e = fresh();
  e.firstPlayerIndex = 2;
  e.currentPlayerIndex = 1;
  e.players[2].isBankrupt = true;
  playQuietTurn(e, rng);  // player 1 ends their turn; seat 2 is skipped
  CHECK(e.currentPlayerIndex == 3);
  CHECK(e.currentTempo == 2);
}

// ===========================================================================
// getLegalActions
// ===========================================================================
static bool hasAction(const LegalActions& l, ActionType type, int tileId = -1) {
  for (const Action& a : l.actions)
    if (a.type == type && (tileId < 0 || a.tileId == tileId)) return true;
  return false;
}

static int countAction(const LegalActions& l, ActionType type) {
  int n = 0;
  for (const Action& a : l.actions)
    if (a.type == type) ++n;
  return n;
}

// Every listed action (steals at the smallest and largest bribe) must be accepted.
static bool allListedAccepted(const GameState& s) {
  const LegalActions l = getLegalActions(s);
  for (const Action& a : l.actions) {
    std::vector<long long> amounts = {a.amount};
    if (a.type == ActionType::AttemptSteal) amounts = {l.minBribe, l.maxBribe};
    for (long long amount : amounts) {
      GameState copy = s;
      std::mt19937 rng(5);
      Action x = a;
      x.amount = amount;
      if (!applyAction(copy, x, rng)) return false;
    }
  }
  return true;
}

static void testLegalActions() {
  // Rolling, with and without the jail fee.
  GameState s = fresh();
  LegalActions l = getLegalActions(s);
  CHECK(l.actions.size() == 1 && hasAction(l, ActionType::RollDice));
  CHECK(l.actions[0].playerId == 0);
  CHECK(allListedAccepted(s));
  s.players[0].turnsInJail = 2;
  l = getLegalActions(s);
  CHECK(l.actions.size() == 2 && hasAction(l, ActionType::PayRetakeFee));
  CHECK(allListedAccepted(s));
  s.players[0].cash = s.config.retakeFee - 1;
  l = getLegalActions(s);
  CHECK(l.actions.size() == 1 && !hasAction(l, ActionType::PayRetakeFee));

  // Unowned tile: buy or decline. Own tile: upgrade or decline.
  GameState a = fresh();
  CHECK(applyRoll(a, 1, 2));
  l = getLegalActions(a);
  CHECK(l.actions.size() == 4 && hasAction(l, ActionType::Buy) && hasAction(l, ActionType::Decline));
  CHECK(allListedAccepted(a));
  GameState b = fresh();
  giveTile(b, 3, 0, Grade::D, b.tiles[3].basePrice);
  CHECK(applyRoll(b, 1, 2));
  l = getLegalActions(b);
  CHECK(l.actions.size() == 3 && hasAction(l, ActionType::Upgrade) && hasAction(l, ActionType::Decline));
  CHECK(allListedAccepted(b));

  // Opponent's tile: pay or steal (no decline); bribes limited by cash.
  GameState c = fresh();
  giveTile(c, 3, 1, Grade::B, 200000);
  CHECK(applyRoll(c, 1, 2));
  l = getLegalActions(c);
  CHECK(l.actions.size() == 2 && hasAction(l, ActionType::PayRent) &&
        hasAction(l, ActionType::AttemptSteal));
  CHECK(!hasAction(l, ActionType::Decline) && !hasAction(l, ActionType::Buy));
  const long long expectedMin = (3LL * rentFor(c, c.tiles[3]) + 1) / 2;
  const long long expectedMax = std::min(c.players[0].cash, (3LL * c.tiles[3].totalInvested + 1) / 2);
  CHECK(l.minBribe == expectedMin && l.maxBribe == expectedMax);
  CHECK(allListedAccepted(c));
  c.players[0].cash = 0;
  l = getLegalActions(c);
  CHECK(l.actions.size() == 1 && hasAction(l, ActionType::PayRent));
  CHECK(l.maxBribe == 0);

  // Special Corner: only affordable targets, split into owned / unowned.
  GameState d = fresh();
  giveTile(d, 7, 0, Grade::D, d.tiles[7].basePrice);
  d.players[0].position = 20;
  resolveLanding(d, d.players[0], d.tiles[20]);
  l = getLegalActions(d);
  CHECK(hasAction(l, ActionType::Decline));
  CHECK(hasAction(l, ActionType::TeleportOwned, 7));
  CHECK(!hasAction(l, ActionType::TeleportUnowned, 7));  // already mine
  CHECK(countAction(l, ActionType::TeleportUnowned) == 87);
  CHECK(allListedAccepted(d));
  d.players[0].cash = d.config.adminFee + 105000;  // can only afford a 100k tile
  l = getLegalActions(d);
  CHECK(hasAction(l, ActionType::TeleportUnowned, 1));   // price 100000
  CHECK(!hasAction(l, ActionType::TeleportUnowned, 2));  // price 110000
  CHECK(allListedAccepted(d));

  // Liquidation: sell any of your own tiles, nothing else.
  GameState e = fresh();
  std::mt19937 rng(1);
  giveTile(e, 2, 0, Grade::A, 300000);
  giveTile(e, 3, 1, Grade::B, 200000);
  giveTile(e, 6, 1, Grade::D, 130000);
  e.players[1].cash = 1000;
  e.currentPlayerIndex = 1;
  CHECK(applyRoll(e, 1, 1));
  CHECK(applyAction(e, act(ActionType::PayRent, 1), rng));
  CHECK(e.phase == Phase::AwaitLiquidation);
  l = getLegalActions(e);
  CHECK(l.actions.size() == 2);
  CHECK(hasAction(l, ActionType::SellTile, 3) && hasAction(l, ActionType::SellTile, 6));
  CHECK(l.actions[0].playerId == 1);
  CHECK(allListedAccepted(e));

  // Nothing is legal once the game is over.
  GameState g = fresh();
  g.phase = Phase::GameOver;
  CHECK(getLegalActions(g).actions.empty());
}

static bool offersGrade(const GameState& s, ActionType type, Grade target, int tid = -1) {
  for (const Action& a : getLegalActions(s).actions)
    if (a.type == type && a.targetGrade == target && a.tileId == tid) return true;
  return false;
}

static void testGradeChoicePurchasesAndUpgradePaths() {
  std::mt19937 rng(17);
  for (Grade target : {Grade::D, Grade::C, Grade::B}) {
    GameState s = fresh();
    CHECK(applyRoll(s, 1, 2));  // unowned tile 3; no doubles
    CHECK(offersGrade(s, ActionType::Buy, target));
    const long long expected = s.tiles[3].basePrice +
        (static_cast<int>(target) - static_cast<int>(Grade::D)) * s.tiles[3].upgradeCost;
    CHECK(applyAction(s, act(ActionType::Buy, 0, 0, -1, target), rng));
    CHECK(s.tiles[3].grade == target && s.tiles[3].ownerId == 0);
    CHECK(s.tiles[3].totalInvested == expected);
    CHECK(s.players[0].cash == 1000000 - expected);
    CHECK(calculateNetWorth(s, s.players[0]) == 1000000);
    CHECK(!applyAction(s, act(ActionType::Upgrade, 0, 0, -1, Grade::A), rng));
  }
  for (Grade from : {Grade::D, Grade::C, Grade::B, Grade::A, Grade::APlus}) {
    for (Grade to : {Grade::D, Grade::C, Grade::B, Grade::A, Grade::APlus}) {
      GameState s = fresh();
      const int invested = s.tiles[3].basePrice + (static_cast<int>(from) - 1) * s.tiles[3].upgradeCost;
      giveTile(s, 3, 0, from, invested);
      CHECK(applyRoll(s, 1, 2));
      const bool allowed = (from == Grade::D && (to == Grade::C || to == Grade::B)) ||
          (from == Grade::C && to == Grade::B) || (from == Grade::B && to == Grade::A) ||
          (from == Grade::A && to == Grade::APlus);
      CHECK(offersGrade(s, ActionType::Upgrade, to) == allowed);
      const auto beforeRng = rng;
      CHECK(applyAction(s, act(ActionType::Upgrade, 0, 0, -1, to), rng) == allowed);
      const long long cost = allowed ? (static_cast<int>(to) - static_cast<int>(from)) * s.tiles[3].upgradeCost : 0;
      CHECK(s.players[0].cash == 1000000 - cost);
      CHECK(s.tiles[3].totalInvested == invested + cost);
      CHECK(s.tiles[3].grade == (allowed ? to : from));
      CHECK(rng == beforeRng);  // investments and rejections never consume RNG
    }
  }
  GameState poor = fresh();
  CHECK(applyRoll(poor, 1, 2));
  poor.players[0].cash = purchasePrice(poor.tiles[3], Grade::C) - 1;
  CHECK(offersGrade(poor, ActionType::Buy, Grade::D));
  CHECK(!offersGrade(poor, ActionType::Buy, Grade::C));
  CHECK(!offersGrade(poor, ActionType::Buy, Grade::B));
  const long long beforeCash = poor.players[0].cash;
  for (Grade bad : {Grade::C, Grade::B, Grade::A, Grade::APlus, static_cast<Grade>(-1), static_cast<Grade>(6)}) {
    CHECK(!applyAction(poor, act(ActionType::Buy, 0, 0, -1, bad), rng));
    CHECK(poor.players[0].cash == beforeCash && poor.tiles[3].ownerId == -1);
    CHECK(poor.phase == Phase::AwaitLandingChoice);
  }
  GameState dbl = fresh();
  CHECK(applyRoll(dbl, 1, 1));
  CHECK(applyAction(dbl, act(ActionType::Buy, 0, 0, -1, Grade::B), rng));
  CHECK(dbl.phase == Phase::AwaitRoll && dbl.currentPlayerIndex == 0);
  CHECK(!applyAction(dbl, act(ActionType::Upgrade, 0, 0, -1, Grade::A), rng));
  CHECK(dbl.tiles[2].grade == Grade::B);
}

static void testPostStealGradeChoices() {
  std::mt19937 rng(19);
  for (Grade from : {Grade::D, Grade::C, Grade::B, Grade::A}) {
    for (bool upgrade : {false, true}) {
      GameState s = fresh();
      const int invested = s.tiles[2].basePrice + (static_cast<int>(from) - 1) * s.tiles[2].upgradeCost;
      giveTile(s, 2, 1, from, invested);
      CHECK(applyRoll(s, 1, 1));
      CHECK(!offersGrade(s, ActionType::Upgrade, Grade::B));  // opponent: rent/steal only
      CHECK(!applyAction(s, act(ActionType::Upgrade, 0, 0, -1, Grade::B), rng));
      const long long bribe = (3LL * invested + 1) / 2;
      CHECK(applyAction(s, act(ActionType::AttemptSteal, 0, bribe), rng));
      CHECK(s.tiles[2].ownerId == 0 && s.tiles[2].grade == from);
      CHECK(s.tiles[2].totalInvested == invested);
      CHECK(s.players[0].cash == 1000000 - bribe && s.players[1].cash == 1000000);
      CHECK(s.players[1].ownedTileIds.empty() && s.players[0].ownedTileIds.size() == 1);
      if (from == Grade::A) {
        CHECK(s.phase == Phase::AwaitRoll && s.currentPlayerIndex == 0 && s.pendingTileId == -1);
        CHECK(!offersGrade(s, ActionType::Upgrade, Grade::APlus));
        CHECK(!applyAction(s, act(ActionType::Upgrade, 0, 0, -1, Grade::APlus), rng));
        continue;
      }
      CHECK(s.phase == Phase::AwaitLandingChoice && s.currentPlayerIndex == 0 && s.pendingTileId == 2);
      const Grade to = from == Grade::D ? Grade::B : static_cast<Grade>(static_cast<int>(from) + 1);
      CHECK(offersGrade(s, ActionType::Upgrade, to));
      const long long cost = upgrade ? upgradePrice(s.tiles[2], to) : 0;
      CHECK(applyAction(s, act(upgrade ? ActionType::Upgrade : ActionType::Decline, 0, 0, -1,
                               upgrade ? to : Grade::None), rng));
      CHECK(s.tiles[2].grade == (upgrade ? to : from));
      CHECK(s.tiles[2].totalInvested == invested + cost);
      CHECK(s.players[0].cash == 1000000 - bribe - cost);
      CHECK(s.phase == Phase::AwaitRoll && s.currentPlayerIndex == 0);  // doubles resume after choice
      CHECK(!applyAction(s, act(ActionType::Upgrade, 0, 0, -1, Grade::APlus), rng));
    }
  }
  GameState noCash = fresh();
  giveTile(noCash, 3, 1, Grade::D, noCash.tiles[3].basePrice);
  CHECK(applyRoll(noCash, 1, 2));
  const long long bribe = (3LL * noCash.tiles[3].totalInvested + 1) / 2;
  noCash.players[0].cash = bribe;
  CHECK(applyAction(noCash, act(ActionType::AttemptSteal, 0, bribe), rng));
  CHECK(noCash.tiles[3].ownerId == 0 && noCash.tiles[3].grade == Grade::D);
  CHECK(noCash.players[0].cash == 0 && noCash.currentPlayerIndex == 1);

  GameState limited = fresh();
  giveTile(limited, 3, 1, Grade::D, limited.tiles[3].basePrice);
  CHECK(applyRoll(limited, 1, 2));
  const long long limitedBribe = (3LL * limited.tiles[3].totalInvested + 1) / 2;
  limited.players[0].cash = limitedBribe + 2 * limited.tiles[3].upgradeCost - 1;
  CHECK(applyAction(limited, act(ActionType::AttemptSteal, 0, limitedBribe), rng));
  CHECK(offersGrade(limited, ActionType::Upgrade, Grade::C));
  CHECK(!offersGrade(limited, ActionType::Upgrade, Grade::B));
  const long long remainder = limited.players[0].cash;
  CHECK(!applyAction(limited, act(ActionType::Upgrade, 0, 0, -1, Grade::B), rng));
  CHECK(limited.players[0].cash == remainder && limited.tiles[3].grade == Grade::D);
  CHECK(applyAction(limited, act(ActionType::Upgrade, 0, 0, -1, Grade::C), rng));
  CHECK(limited.tiles[3].grade == Grade::C);

  GameState last = fresh();
  giveTile(last, 3, 1, Grade::D, last.tiles[3].basePrice);
  last.currentTempo = last.config.maxTempo;
  last.currentPlayerIndex = 3;
  CHECK(applyRoll(last, 1, 2));
  CHECK(applyAction(last, act(ActionType::AttemptSteal, 3, (3LL * last.tiles[3].totalInvested + 1) / 2), rng));
  CHECK(last.phase == Phase::AwaitLandingChoice);  // cannot finish the game before the earned choice
  CHECK(applyAction(last, act(ActionType::Upgrade, 3, 0, -1, Grade::B), rng));
  CHECK(last.phase == Phase::GameOver && last.tiles[3].grade == Grade::B);

  GameState exam = fresh();
  giveTile(exam, 3, 1, Grade::B, exam.tiles[3].basePrice + 2 * exam.tiles[3].upgradeCost);
  exam.currentTempo = 5;
  exam.currentPlayerIndex = 3;
  CHECK(applyRoll(exam, 1, 2));
  const long long examBribe = (3LL * exam.tiles[3].totalInvested + 1) / 2;
  CHECK(applyAction(exam, act(ActionType::AttemptSteal, 3, examBribe), rng));
  CHECK(exam.currentTempo == 5);  // finish the upgrade before entering Exam Season
  CHECK(applyAction(exam, act(ActionType::Upgrade, 3, 0, -1, Grade::A), rng));
  CHECK(exam.currentTempo == 6);
  CHECK(exam.players[3].cash == 1000000 - examBribe - exam.tiles[3].upgradeCost + exam.config.examBonusA);
}

static void testCornerGradeChoices() {
  std::mt19937 rng(23);
  for (Grade target : {Grade::D, Grade::C, Grade::B}) {
    GameState s = fresh();
    s.players[0].position = 20;
    resolveLanding(s, s.players[0], s.tiles[20]);
    CHECK(offersGrade(s, ActionType::TeleportUnowned, target, 3));
    const long long cost = s.config.adminFee + purchasePrice(s.tiles[3], target);
    CHECK(applyAction(s, act(ActionType::TeleportUnowned, 0, 0, 3, target), rng));
    CHECK(s.players[0].cash == 1000000 - cost && s.players[0].position == 3);
    CHECK(s.tiles[3].grade == target && s.tiles[3].totalInvested == cost - s.config.adminFee);
  }
  for (Grade from : {Grade::D, Grade::C, Grade::B, Grade::A}) {
    GameState s = fresh();
    const int invested = s.tiles[3].basePrice + (static_cast<int>(from) - 1) * s.tiles[3].upgradeCost;
    giveTile(s, 3, 0, from, invested);
    s.players[0].position = 20;
    resolveLanding(s, s.players[0], s.tiles[20]);
    const Grade target = from == Grade::D ? Grade::B : static_cast<Grade>(static_cast<int>(from) + 1);
    const long long cost = s.config.adminFee + upgradePrice(s.tiles[3], target);
    const long long before = s.players[0].cash;
    CHECK(!applyAction(s, act(ActionType::TeleportOwned, 0, 0, 3, from), rng));
    CHECK(s.players[0].cash == before && s.players[0].position == 20);
    CHECK(applyAction(s, act(ActionType::TeleportOwned, 0, 0, 3, target), rng));
    CHECK(s.tiles[3].grade == target && s.players[0].cash == before - cost);
    CHECK(s.tiles[3].totalInvested == invested + cost - s.config.adminFee);
  }
}


static void giveAuction(GameState& s, int tid, int owner, long long paid = 50000) {
  Tile& t = s.tiles[tid];
  t.ownerId = owner; t.grade = Grade::None; t.totalInvested = paid;
  t.basePrice = paid + s.config.auctionEscalation;
  s.players[owner].ownedTileIds.push_back(tid);
}
static void testAuctions() {
  std::mt19937 rng(31);
  GameState s = fresh();
  int auctions = 0, cards = 0, institutes = 0;
  for (const auto& t : s.tiles) {
    auctions += t.type == TileType::Auction;
    cards += t.type == TileType::Other;
    institutes += t.type == TileType::Institute;
  }
  CHECK(auctions == 4 && cards == 2 && institutes == 30);
  CHECK(s.tiles[5].basePrice == 120000 && s.tiles[5].upgradeCost == 60000 && s.tiles[5].baseRent == 12000);
  s.players[0].position = 4;
  resolveLanding(s, s.players[0], s.tiles[4]);
  CHECK(getLegalActions(s).actions.size() == 2);
  CHECK(!applyAction(s, act(ActionType::Buy, 0, 0, -1, Grade::D), rng));
  CHECK(applyAction(s, act(ActionType::Buy, 0), rng));
  CHECK(s.players[0].cash == 950000 && s.tiles[4].basePrice == 70000 && s.tiles[4].totalInvested == 50000);
  s.currentPlayerIndex = 1; s.phase = Phase::AwaitRoll; s.players[1].position = 4;
  resolveLanding(s, s.players[1], s.tiles[4]);
  CHECK(s.players[1].cash == 975000 && s.players[0].cash == 975000);
  CHECK(!hasAction(getLegalActions(s), ActionType::AttemptSteal));
  CHECK(applyAction(s, act(ActionType::Buy, 1), rng));
  CHECK(s.players[1].cash == 905000 && s.players[0].cash == 975000);
  CHECK(s.tiles[4].ownerId == 1 && s.tiles[4].basePrice == 90000 && s.tiles[4].totalInvested == 70000);
  CHECK(s.players[0].ownedTileIds.empty());

  GameState debt = fresh();
  giveAuction(debt, 4, 1);
  giveTile(debt, 2, 0, Grade::B, 200000);
  debt.players[0].cash = 0; debt.players[0].position = 4;
  resolveLanding(debt, debt.players[0], debt.tiles[4]);
  CHECK(debt.phase == Phase::AwaitLiquidation && debt.debt.auctionTileId == 4);
  CHECK(applyAction(debt, act(ActionType::SellTile, 0, 0, 2), rng));
  CHECK(debt.phase == Phase::AwaitLandingChoice && debt.players[0].cash == 75000);
  CHECK(debt.players[1].cash == 1025000);
  CHECK(applyAction(debt, act(ActionType::Buy, 0), rng));
  CHECK(debt.players[0].cash == 5000 && debt.tiles[4].basePrice == 90000);

  GameState sold = fresh();
  giveAuction(sold, 4, 0);
  giveAuction(sold, 13, 1);
  sold.players[0].cash = 0; sold.players[0].position = 13;
  resolveLanding(sold, sold.players[0], sold.tiles[13]);
  CHECK(applyAction(sold, act(ActionType::SellTile, 0, 0, 4), rng));
  CHECK(sold.tiles[4].ownerId == -1 && sold.tiles[4].totalInvested == 0 && sold.tiles[4].basePrice == 70000);
  CHECK(sold.players[0].cash == 0 && sold.phase != Phase::AwaitLiquidation);

  GameState holiday = fresh(); giveAuction(holiday, 4, 1);
  holiday.rentFreeTurnsLeft = 1; holiday.players[0].position = 4;
  resolveLanding(holiday, holiday.players[0], holiday.tiles[4]);
  CHECK(holiday.players[0].cash == 1000000 && holiday.phase == Phase::AwaitLandingChoice);
  GameState corner = fresh(); giveAuction(corner, 4, 0); giveAuction(corner, 13, 1);
  corner.players[0].position = 20; resolveLanding(corner, corner.players[0], corner.tiles[20]);
  CHECK(hasAction(getLegalActions(corner), ActionType::TeleportOwned, 4));
  CHECK(!hasAction(getLegalActions(corner), ActionType::TeleportUnowned, 25));
  CHECK(!hasAction(getLegalActions(corner), ActionType::TeleportOwned, 13));
  CHECK(applyAction(corner, act(ActionType::TeleportOwned, 0, 0, 4), rng));
  CHECK(corner.players[0].cash == 980000 && corner.players[0].position == 4 && corner.tiles[4].basePrice == 70000);
  for (bool takeover : {false, true}) {
    GameState win = fresh();
    for (int tid : {4, 13, 25}) giveAuction(win, tid, 0);
    if (takeover) giveAuction(win, 34, 1);
    win.players[1].cash = 999999999; win.lastDice = {1, 1}; win.players[0].consecutiveDoubles = 1;
    win.players[0].position = 34; resolveLanding(win, win.players[0], win.tiles[34]);
    CHECK(applyAction(win, act(ActionType::Buy, 0), rng));
    CHECK(win.phase == Phase::GameOver && win.winnerId == 0 && countOwnedAuctions(win, 0) == 4);
    CHECK(getLegalActions(win).actions.empty());
  }
}

int main() {
  testAuctions();
  testGradeChoicePurchasesAndUpgradePaths();
  testPostStealGradeChoices();
  testCornerGradeChoices();
  testStartingCashAndRentScaling();
  testDoublesAndTripleDoublesJail();
  testNonDoubleResetsStreakAndBuy();
  testPassingStartPaysSalaryOnly();
  testRentAndLiquidation();
  testExpulsion();
  testStealSuccessKeepsGrade();
  testStealFailureSinglePenaltyAndValidation();
  testJailRules();
  testAcademicWarning();
  testSpecialCorner();
  testExamSeason();
  testGameEndAtMaxTempo();
  testTempoAdvancesAfterEveryone();
  testIllegalActionsRejected();
  testDeckContentAndCycle();
  testCardMoney();
  testCardPerProperty();
  testCardPovertySubsidy();
  testSubsidyTieIsRandom();
  testCardJailAndTeleport();
  testCardSkipTurn();
  testCardHalveDice();
  testCardStealBuff();
  testStealBonusExpires();
  testCardRentHoliday();
  testCardsSwitchAndRegularTiles();
  testRandomFirstPlayer();
  testTempoBoundaryFollowsFirstPlayer();
  testLegalActions();

  std::cout << (g_total - g_failed) << "/" << g_total << " checks passed\n";
  return g_failed == 0 ? 0 : 1;
}
