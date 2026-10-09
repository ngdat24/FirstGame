#pragma once

#include <string>
#include <vector>

// What a card does. The numeric meaning of Card::value depends on the effect.
enum class CardEffectType {
  MoneyFlat,         // value > 0: the bank pays you; value < 0: you pay the bank
                     //   (a debt you cannot cover forces liquidation)
  MoneyPerProperty,  // you pay `value` x grade weight (D=1, C=2, B=4, A=8) for
                     //   EVERY Institute you own -> hits big landlords hardest
  PovertySubsidy,    // the player with the lowest net worth gets `value` from the
                     //   bank (whoever drew the card)
  GoToJail,          // straight to the Retake Exam room
  TeleportTile,      // advance to tile id `value` (passing Start pays salary),
                     //   then that tile's landing rules apply
  SkipTurn,          // your next turn is skipped (value unused)
  HalveNextDice,     // your next move is halved, rounded down (value unused)
  StealBuff,         // +`value` percentage points on your next steal attempt
  RentHoliday        // nobody pays rent for one full round (value unused)
};

struct Card {
  int id = 0;
  std::string title;        // short name shown on the card
  std::string description;  // what happens, shown to the players
  CardEffectType effectType = CardEffectType::MoneyFlat;
  long long value = 0;
};

// The catalogue never changes during a game; only the two piles do.
// Piles hold indices into `cards`, so no Card (or string) is ever copied.
// The top of the draw pile is drawPile.back().
struct CardDeck {
  std::vector<Card> cards;
  std::vector<int> drawPile;
  std::vector<int> discardPile;
};
