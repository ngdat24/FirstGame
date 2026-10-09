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

static Action act(ActionType type, int playerId, long long amount = 0, int tileId = -1) {
  Action a;
  a.type = type;
  a.playerId = playerId;
  a.amount = amount;
  a.tileId = tileId;
  return a;
}

static GameState fresh() { return makeNewGame(4, makeBoard()); }

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
  giveTile(s, 3, 1, Grade::B, 400000);  // sells for 200000
  const long long rent = rentFor(s, s.tiles[2]);  // baseRent 11000 x 16 = 176000
  CHECK(rent == 11000LL * 16);
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
  CHECK(s.players[1].cash == 1000 + 200000 - rent);
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
  CHECK(applyAction(s, act(ActionType::AttemptSteal, 1, 150000), rng));  // 100% chance
  CHECK(s.tiles[2].ownerId == 1);
  CHECK(s.tiles[2].grade == Grade::B);
  CHECK(s.tiles[2].totalInvested == 150000);
  CHECK(s.players[1].cash == s.config.startCash - 150000);
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

  CHECK(!applyAction(s, act(ActionType::AttemptSteal, 1, 0), rng));         // bribe must be > 0
  CHECK(!applyAction(s, act(ActionType::AttemptSteal, 1, -5), rng));        // negative
  CHECK(!applyAction(s, act(ActionType::AttemptSteal, 1, 2000000), rng));   // more than cash
  CHECK(s.phase == Phase::AwaitLandingChoice);                              // nothing changed

  CHECK(applyAction(s, act(ActionType::AttemptSteal, 1, 1), rng));  // ~0.0007% chance
  CHECK(s.tiles[2].ownerId == 0);                  // steal failed
  CHECK(s.players[1].cash == s.config.startCash - 1);  // lost the bribe only
  CHECK(s.players[0].cash == ownerCash);           // and owes NO rent
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

  // Teleport to an owned institute: upgrades it.
  GameState u = fresh();
  giveTile(u, 7, 0, Grade::D, u.tiles[7].basePrice);
  u.players[0].position = 20;
  resolveLanding(u, u.players[0], u.tiles[20]);
  CHECK(applyAction(u, act(ActionType::TeleportOwned, 0, 0, 7), rng));
  CHECK(u.tiles[7].grade == Grade::C);
  CHECK(u.players[0].position == 7);

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
  giveTile(s, 2, 0, Grade::A, 300000);
  giveTile(s, 3, 0, Grade::B, 200000);
  giveTile(s, 4, 0, Grade::C, 200000);  // C and D earn nothing
  const long long base = s.players[0].cash;
  const long long bonus = s.config.examBonusA + s.config.examBonusB;

  s.currentTempo = 12;
  examSeasonPayout(s);
  CHECK(s.players[0].cash == base);
  s.currentTempo = 13;
  examSeasonPayout(s);
  CHECK(s.players[0].cash == base + bonus);
  s.currentTempo = 16;
  examSeasonPayout(s);
  CHECK(s.players[0].cash == base + 2 * bonus);
  s.currentTempo = 17;  // first week of the second semester
  examSeasonPayout(s);
  CHECK(s.players[0].cash == base + 2 * bonus);
  s.currentTempo = 29;  // week 13 of the second semester
  examSeasonPayout(s);
  CHECK(s.players[0].cash == base + 3 * bonus);
}

static void testGameEndAtTempo64() {
  GameState s = fresh();
  std::mt19937 rng(1);
  s.currentTempo = 64;
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

// Current player rolls onto tile 5 (an "Other" tile) from Start.
static void landOnCardTile(GameState& s) {
  s.players[s.currentPlayerIndex].position = 0;
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
  giveTile(s, 2, 0, Grade::A, 300000);  // weight 8
  giveTile(s, 3, 0, Grade::D, 120000);  // weight 1
  landOnCardTile(s);
  CHECK(s.players[0].cash == s.config.startCash - 30000LL * (8 + 1));

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
  d.players[0].position = 1;
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

  // A 100% bonus makes even a 1-VND bribe certain, and the bonus is consumed.
  GameState t = fresh();
  giveTile(t, 2, 0, Grade::B, 150000);
  t.currentPlayerIndex = 1;
  t.players[1].stealBonusPercent = 100;
  CHECK(applyRoll(t, 1, 1));
  CHECK(applyAction(t, act(ActionType::AttemptSteal, 1, 1), rng));
  CHECK(t.tiles[2].ownerId == 1);
  CHECK(t.players[1].stealBonusPercent == 0);

  // A failed attempt also uses it up.
  GameState u = fresh();
  giveTile(u, 2, 0, Grade::B, 150000);
  u.currentPlayerIndex = 1;
  u.players[1].stealBonusPercent = 1;  // 1 / 150000 + 1% is still a long shot
  CHECK(applyRoll(u, 1, 1));
  CHECK(applyAction(u, act(ActionType::AttemptSteal, 1, 1), rng));
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

int main() {
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
  testGameEndAtTempo64();
  testTempoAdvancesAfterEveryone();
  testIllegalActionsRejected();
  testDeckContentAndCycle();
  testCardMoney();
  testCardPerProperty();
  testCardPovertySubsidy();
  testCardJailAndTeleport();
  testCardSkipTurn();
  testCardHalveDice();
  testCardStealBuff();
  testCardRentHoliday();
  testCardsSwitchAndRegularTiles();

  std::cout << (g_total - g_failed) << "/" << g_total << " checks passed\n";
  return g_failed == 0 ? 0 : 1;
}
