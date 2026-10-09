// Headless simulation: bots play full games through applyAction().
//
//   ./sim                      -> 1000 games, seed 12345
//   ./sim --games 5000 --seed 7
//   ./sim --verbose --seed 3   -> print the full event log of ONE game
//   ./sim --nocards            -> same game with the card tiles switched off (A/B test)
//   ./sim --salary 20000 --cash 400000 --rentx 3   -> try a harsher economy
//   ./sim --flatrent --exambonus 0 --stealprob 0.4  -> experiment flags
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>

#include "GameLogic.h"
#include "board_data.h"

namespace {

double g_stealChance = 0.15;  // how often the bot tries to steal instead of paying rent

bool chance(std::mt19937& rng, double p) {
  return std::uniform_real_distribution<double>(0.0, 1.0)(rng) < p;
}

// A deliberately simple bot: buys/upgrades while keeping a cash reserve,
// occasionally gambles on a 50% steal, sells its cheapest tile when in debt.
Action botChoose(const GameState& s, std::mt19937& rng) {
  const Player& p = s.players[s.currentPlayerIndex];
  const GameConfig& cfg = s.config;
  const long long reserve = 100000;
  Action a;
  a.playerId = p.id;

  switch (s.phase) {
    case Phase::AwaitRoll:
      if (p.turnsInJail > 0 && p.cash >= cfg.retakeFee + reserve && chance(rng, 0.5))
        a.type = ActionType::PayRetakeFee;
      else
        a.type = ActionType::RollDice;
      return a;

    case Phase::AwaitLandingChoice: {
      const Tile& t = s.tiles[s.pendingTileId];
      if (t.ownerId == -1) {
        a.type = (p.cash - t.basePrice >= reserve) ? ActionType::Buy : ActionType::Decline;
      } else if (t.ownerId == p.id) {
        a.type = (p.cash - t.upgradeCost >= reserve) ? ActionType::Upgrade : ActionType::Decline;
      } else {
        const long long bribe = t.totalInvested / 2;  // ~50% success chance
        if (bribe > 0 && p.cash - bribe >= reserve && chance(rng, g_stealChance)) {
          a.type = ActionType::AttemptSteal;
          a.amount = bribe;
        } else {
          a.type = ActionType::PayRent;
        }
      }
      return a;
    }

    case Phase::AwaitCornerChoice: {
      const long long avail = p.cash - cfg.adminFee - reserve;
      a.type = ActionType::Decline;
      for (const Tile& t : s.tiles) {
        if (t.type != TileType::Institute) continue;
        if (t.ownerId == -1 && t.basePrice <= avail) {
          a.type = ActionType::TeleportUnowned;
          a.tileId = t.id;
          break;
        }
        if (t.ownerId == p.id && t.grade != Grade::A && t.upgradeCost <= avail) {
          a.type = ActionType::TeleportOwned;
          a.tileId = t.id;
          break;
        }
      }
      return a;
    }

    case Phase::AwaitLiquidation: {
      int best = -1;
      for (int id : p.ownedTileIds)
        if (best < 0 || s.tiles[id].totalInvested < s.tiles[best].totalInvested) best = id;
      a.type = ActionType::SellTile;
      a.tileId = best;
      return a;
    }

    default:
      return a;
  }
}

// Returns "" if the state is consistent, otherwise a description of the problem.
std::string checkInvariants(const GameState& s) {
  const int nTiles = static_cast<int>(s.tiles.size());
  for (const Tile& t : s.tiles) {
    if (t.ownerId == -1) {
      if (t.grade != Grade::None || t.totalInvested != 0)
        return "unowned tile " + std::to_string(t.id) + " has grade/investment";
    } else {
      if (t.ownerId < 0 || t.ownerId >= static_cast<int>(s.players.size()))
        return "tile " + std::to_string(t.id) + " has invalid owner";
      const Player& o = s.players[t.ownerId];
      if (o.isBankrupt) return "expelled player owns tile " + std::to_string(t.id);
      if (std::count(o.ownedTileIds.begin(), o.ownedTileIds.end(), t.id) != 1)
        return "owner list mismatch for tile " + std::to_string(t.id);
      if (t.grade == Grade::None || t.totalInvested <= 0)
        return "owned tile " + std::to_string(t.id) + " has no grade/investment";
    }
  }
  for (const Player& p : s.players) {
    if (p.cash < 0) return p.name + " has negative cash";
    if (p.position < 0 || p.position >= nTiles) return p.name + " has invalid position";
    if (p.isBankrupt && !p.ownedTileIds.empty()) return p.name + " expelled but owns tiles";
    if (p.skipTurns < 0 || p.stealBonusPercent < 0 || p.stealBonusPercent > 100)
      return p.name + " has an invalid status effect";
    for (int id : p.ownedTileIds)
      if (s.tiles[id].ownerId != p.id) return p.name + " lists a tile it does not own";
  }
  // Card deck: every card index appears at most once across the two piles.
  std::vector<int> seen(s.deck.cards.size(), 0);
  for (int i : s.deck.drawPile) {
    if (i < 0 || i >= static_cast<int>(seen.size()) || ++seen[i] > 1) return "deck draw pile is corrupt";
  }
  for (int i : s.deck.discardPile) {
    if (i < 0 || i >= static_cast<int>(seen.size()) || ++seen[i] > 1) return "deck discard pile is corrupt";
  }
  if (s.rentFreeTurnsLeft < 0) return "negative rent holiday counter";
  return "";
}

struct Stats {
  int games = 0;
  int illegalActions = 0;
  int invariantFailures = 0;
  int hitActionCap = 0;
  int endedEarly = 0;  // only one player left before the final tempo
  long long totalActions = 0;
  long long totalTempos = 0;
  long long totalExpelled = 0;
  double winnerWorthSum = 0;
  std::array<int, 4> winsBySeat{};
  int midgameGames = 0;       // games still running when Tempo 33 starts
  int midgameLeaderWon = 0;   // ...where the Tempo-32 net-worth leader won
};

}  // namespace

int main(int argc, char** argv) {
  int games = 1000;
  unsigned seed = 12345;
  bool verbose = false;
  long long salaryOverride = -1;  // -1 = keep the default from GameConfig
  long long cashOverride = -1;
  int rentMultiplier = 1;
  bool cardsEnabled = true;     // --nocards turns the card tiles into no-ops
  bool flatRent = false;        // rent factors 1,2,3,4 instead of 1,2,4,8
  double examBonusScale = 1.0;  // 0 disables Exam Season bonuses
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--games" && i + 1 < argc) games = std::atoi(argv[++i]);
    else if (arg == "--seed" && i + 1 < argc) seed = static_cast<unsigned>(std::atoi(argv[++i]));
    else if (arg == "--verbose") verbose = true;
    else if (arg == "--salary" && i + 1 < argc) salaryOverride = std::atoll(argv[++i]);
    else if (arg == "--cash" && i + 1 < argc) cashOverride = std::atoll(argv[++i]);
    else if (arg == "--rentx" && i + 1 < argc) rentMultiplier = std::atoi(argv[++i]);
    else if (arg == "--flatrent") flatRent = true;
    else if (arg == "--nocards") cardsEnabled = false;
    else if (arg == "--exambonus" && i + 1 < argc) examBonusScale = std::atof(argv[++i]);
    else if (arg == "--stealprob" && i + 1 < argc) g_stealChance = std::atof(argv[++i]);
  }
  if (verbose) games = 1;

  const int kPlayers = 4;
  const long long kActionCap = 200000;  // safety net against infinite loops
  Stats st;

  for (int g = 0; g < games; ++g) {
    std::mt19937 rng(seed + static_cast<unsigned>(g));
    GameState state = makeNewGame(kPlayers, makeBoard(), seed + static_cast<unsigned>(g));
    state.logEnabled = verbose;
    state.config.cardsEnabled = cardsEnabled;
    if (salaryOverride >= 0) state.config.salary = salaryOverride;
    if (cashOverride >= 0)
      for (Player& p : state.players) p.cash = cashOverride;
    if (flatRent) {
      const int flat[5] = {0, 1, 2, 3, 4};
      for (int i = 0; i < 5; ++i) state.config.rentFactor[i] = flat[i];
    }
    for (int& f : state.config.rentFactor) f *= rentMultiplier;
    state.config.examBonusA = static_cast<long long>(state.config.examBonusA * examBonusScale);
    state.config.examBonusB = static_cast<long long>(state.config.examBonusB * examBonusScale);

    long long actions = 0;
    int midLeader = -1;  // net-worth leader at the end of Tempo 32
    while (state.phase != Phase::GameOver && actions < kActionCap) {
      const Action a = botChoose(state, rng);
      if (!applyAction(state, a, rng)) {
        ++st.illegalActions;
        if (verbose) std::cout << "ILLEGAL ACTION from bot (phase " << static_cast<int>(state.phase) << ")\n";
        break;
      }
      ++actions;
      if (midLeader < 0 && state.currentTempo >= 33 && state.phase != Phase::GameOver) {
        long long best = -1;
        for (const Player& p : state.players) {
          if (p.isBankrupt) continue;
          const long long w = calculateNetWorth(state, p);
          if (w > best) { best = w; midLeader = p.id; }
        }
      }
      if (verbose) {
        for (const std::string& line : state.eventLog) std::cout << line << "\n";
        state.eventLog.clear();
      }
      if (games <= 2000) {
        const std::string problem = checkInvariants(state);
        if (!problem.empty()) {
          ++st.invariantFailures;
          std::cout << "INVARIANT FAILED (game " << g << "): " << problem << "\n";
          break;
        }
      }
    }

    ++st.games;
    st.totalActions += actions;
    if (state.phase != Phase::GameOver) {
      ++st.hitActionCap;
      continue;
    }
    st.totalTempos += state.currentTempo;
    st.totalExpelled += kPlayers - countAlivePlayers(state);
    if (state.currentTempo < state.config.maxTempo) ++st.endedEarly;
    if (midLeader >= 0 && state.winnerId >= 0) {
      ++st.midgameGames;
      if (state.winnerId == midLeader) ++st.midgameLeaderWon;
    }
    if (state.winnerId >= 0) {
      ++st.winsBySeat[state.winnerId];
      st.winnerWorthSum += static_cast<double>(calculateNetWorth(state, state.players[state.winnerId]));
    }
  }

  if (verbose) return 0;

  const double n = st.games;
  std::cout << std::fixed << std::setprecision(1);
  std::cout << "Games simulated:        " << st.games << "\n";
  std::cout << "Illegal bot actions:    " << st.illegalActions << "\n";
  std::cout << "Invariant failures:     " << st.invariantFailures << "\n";
  std::cout << "Games hitting the cap:  " << st.hitActionCap << "\n";
  std::cout << "Avg actions per game:   " << st.totalActions / n << "\n";
  std::cout << "Avg final tempo:        " << st.totalTempos / n << "\n";
  std::cout << "Avg players expelled:   " << st.totalExpelled / n << " / " << kPlayers << "\n";
  std::cout << "Ended before tempo 64:  " << 100.0 * st.endedEarly / n << "%\n";
  std::cout << "Avg winner net worth:   " << st.winnerWorthSum / n << "\n";
  if (st.midgameGames > 0)
    std::cout << "Midgame leader wins:    " << 100.0 * st.midgameLeaderWon / st.midgameGames
              << "%  (of " << st.midgameGames << " games reaching Tempo 33)\n";
  std::cout << "Win rate by seat:       ";
  for (int i = 0; i < kPlayers; ++i)
    std::cout << "P" << (i + 1) << " " << 100.0 * st.winsBySeat[i] / n << "%  ";
  std::cout << "\n";
  return (st.illegalActions || st.invariantFailures || st.hitActionCap) ? 1 : 0;
}
