// Headless simulation: bots play full games through applyAction().
//
//   ./sim                      -> 1000 games, seed 12345
//   ./sim --games 5000 --seed 7
//   ./sim --verbose --seed 3   -> print the full event log of ONE game
//   ./sim --nocards            -> same game with the card tiles switched off (A/B test)
//   ./sim --fuzz               -> also throw garbage actions at the engine; rejected ones must
//                                 never change the state
//   ./sim --salary 20000 --cash 400000 --rentx 3   -> try a harsher economy
//   ./sim --flatrent --exambonus 0 --stealprob 0.4  -> experiment flags
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

#include "GameLogic.h"
#include "board_data.h"
#include "simulation_metrics.h"

namespace {

double g_stealChance = 0.15;  // how often the bot tries to steal instead of paying rent

struct BotConfig {
  long long reserve = 100000;
  bool upgradeFirst = false;
  bool highestGrade = false;  // default retains buying D / one-step upgrades for comparisons
};

bool chance(std::mt19937& rng, double p) {
  return std::uniform_real_distribution<double>(0.0, 1.0)(rng) < p;
}

// A deliberately simple bot: buys/upgrades while keeping a cash reserve,
// occasionally gambles on a steal, sells its cheapest tile when in debt.
Action botChoose(const GameState& s, std::mt19937& rng, const BotConfig& bot = BotConfig{}) {
  const Player& p = s.players[s.currentPlayerIndex];
  const GameConfig& cfg = s.config;
  const long long reserve = bot.reserve;
  const LegalActions legal = getLegalActions(s);
  const auto investment = [&](ActionType type, int tileId = -1) {
    Action selected{ActionType::Decline, p.id};
    for (const Action& candidate : legal.actions) {
      if (candidate.type != type || candidate.tileId != tileId) continue;
      const long long cost = investmentActionCost(s, candidate);
      if (cost >= 0 && cost <= p.cash - reserve &&
          (selected.type == ActionType::Decline || (bot.highestGrade && candidate.targetGrade > selected.targetGrade)))
        selected = candidate;
    }
    return selected;
  };
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
      if (t.type == TileType::Auction) {
        if (p.cash >= t.basePrice && countOwnedAuctions(s, p.id) == 3)
          return {ActionType::Buy, p.id};
        return investment(ActionType::Buy);
      }
      if (t.ownerId == -1) {
        return investment(ActionType::Buy);
      } else if (t.ownerId == p.id) {
        return investment(ActionType::Upgrade);
      } else {
        const long long minB = (3LL * rentFor(s, t) + 1) / 2;
        const long long fullB = (3LL * t.totalInvested + 1) / 2;
        const long long bribe = (minB + fullB) / 2;  // 50% steal chance
        if (t.grade != Grade::APlus && minB < fullB && p.cash >= bribe && p.cash - bribe >= reserve && chance(rng, g_stealChance)) {
          a.type = ActionType::AttemptSteal;
          a.amount = bribe;
        } else {
          a.type = ActionType::PayRent;
        }
      }
      return a;
    }

    case Phase::AwaitCornerChoice: {
      a.type = ActionType::Decline;
      if (bot.upgradeFirst) {
        int best = -1;
        Action selected = a;
        for (const Tile& t : s.tiles) {
          if (t.ownerId != p.id || t.grade == Grade::APlus) continue;
          const Action candidate = investment(ActionType::TeleportOwned, t.id);
          if (candidate.type != ActionType::Decline && (best < 0 || t.grade > s.tiles[best].grade)) {
            best = t.id;
            selected = candidate;
          }
        }
        if (best >= 0) return selected;
      }
      for (const Tile& t : s.tiles) {
        if (t.type != TileType::Institute) continue;
        if (t.ownerId == -1) a = investment(ActionType::TeleportUnowned, t.id);
        else if (t.ownerId == p.id && t.grade != Grade::APlus) a = investment(ActionType::TeleportOwned, t.id);
        if (a.type != ActionType::Decline) break;
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

// ---- Fuzzing: throw garbage at applyAction -------------------------------------------------
unsigned long long mixHash(unsigned long long h, long long v) {
  return h ^ (static_cast<unsigned long long>(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}

// Fingerprint of every rule-relevant field (not the rng engines or the log).
unsigned long long stateDigest(const GameState& s) {
  unsigned long long h = 1469598103934665603ULL;
  h = mixHash(h, s.currentTempo);
  h = mixHash(h, s.currentPlayerIndex);
  h = mixHash(h, s.firstPlayerIndex);
  h = mixHash(h, static_cast<int>(s.phase));
  h = mixHash(h, s.pendingTileId);
  h = mixHash(h, s.debt.amount);
  h = mixHash(h, s.debt.creditorId);
  h = mixHash(h, s.debt.moveSteps);
  h = mixHash(h, s.debt.auctionTileId);
  h = mixHash(h, s.winnerId);
  h = mixHash(h, s.rentFreeTurnsLeft);
  h = mixHash(h, s.lastDice.first);
  h = mixHash(h, s.lastDice.second);
  for (const Player& p : s.players) {
    h = mixHash(h, p.cash);
    h = mixHash(h, p.position);
    h = mixHash(h, p.turnsInJail);
    h = mixHash(h, p.consecutiveDoubles);
    h = mixHash(h, p.isBankrupt);
    h = mixHash(h, p.skipTurns);
    h = mixHash(h, p.halveNextDice);
    h = mixHash(h, p.stealBonusPercent);
    h = mixHash(h, p.stealBonusUntilTempo);
    for (int id : p.ownedTileIds) h = mixHash(h, id);
    h = mixHash(h, -1);
  }
  for (const Tile& t : s.tiles) {
    h = mixHash(h, t.basePrice);
    h = mixHash(h, t.baseRent);
    h = mixHash(h, t.ownerId);
    h = mixHash(h, static_cast<int>(t.grade));
    h = mixHash(h, t.totalInvested);
  }
  for (int i : s.deck.drawPile) h = mixHash(h, i);
  h = mixHash(h, -2);
  for (int i : s.deck.discardPile) h = mixHash(h, i);
  return h;
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
      if ((t.type != TileType::Auction && t.grade == Grade::None) || t.totalInvested <= 0)
        return "owned tile " + std::to_string(t.id) + " has no grade/investment";
    }
    if (t.type == TileType::Auction && (t.grade != Grade::None || t.upgradeCost != 0 || t.basePrice < s.config.auctionStartPrice))
      return "auction tile " + std::to_string(t.id) + " has invalid grade/price";
  }
  for (const Player& p : s.players) {
    if (p.cash < 0) return p.name + " has negative cash";
    if (p.position < 0 || p.position >= nTiles) return p.name + " has invalid position";
    if (p.isBankrupt && !p.ownedTileIds.empty()) return p.name + " expelled but owns tiles";
    if (p.skipTurns < 0 || p.stealBonusPercent < 0 || p.stealBonusPercent > 100 || p.stealBonusUntilTempo < 0)
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
  int completedGames = 0;
  long long invariantChecks = 0;
  int illegalActions = 0;
  int invariantFailures = 0;
  int hitActionCap = 0;
  int endedEarly = 0;  // only one player left before the final tempo
  long long totalActions = 0;
  long long totalTempos = 0;
  long long totalExpelled = 0;
  double winnerWorthSum = 0;
  std::array<int, 4> winsBySeat{};
  std::array<int, 4> winsByTurnOrder{};  // 0 = the player who went first
  long long fuzzRejected = 0;
  long long fuzzAccepted = 0;
  long long fuzzMutations = 0;  // a REJECTED action that still changed the state (must be 0)
  long long legalMismatches = 0;  // getLegalActions disagreeing with applyAction (must be 0)
  long long legalChecked = 0;
  int gamesWithExpulsion = 0;
  double endTiles[6] = {0, 0, 0, 0, 0, 0};  // owned Institutes at the end, indexed by Grade (None..APlus)
  int gamesWithGradeA = 0;
  double startWorthSum = 0;   // total net worth of all players at the start of each game
  double endWorthSum = 0;     // ...and at the end (expelled players count as 0)
  long long rollsSum = 0;     // dice rolls (quick, no thinking)
  long long decisionsSum = 0; // real choices: non-roll actions with more than one legal option
  int midgameGames = 0;       // games still running at the halfway tempo
  int midgameLeaderWon = 0;   // ...where the halfway net-worth leader won
};

}  // namespace

// Is `a` one of the actions getLegalActions() allows? Only the fields that matter for the
// action type are compared (e.g. Buy ignores tileId and amount).
bool isListedLegal(const LegalActions& legal, const Action& a) {
  for (const Action& l : legal.actions) {
    if (l.type != a.type || l.playerId != a.playerId) continue;
    if (a.targetGrade != Grade::None && a.targetGrade != l.targetGrade) continue;
    switch (a.type) {
      case ActionType::AttemptSteal:
        if (a.amount >= legal.minBribe && a.amount <= legal.maxBribe) return true;
        break;
      case ActionType::TeleportUnowned:
      case ActionType::TeleportOwned:
      case ActionType::SellTile:
        if (l.tileId == a.tileId) return true;
        break;
      default:
        return true;
    }
  }
  return false;
}

// Soundness: every listed action (steals at the smallest, a middle and the largest bribe)
// must really be accepted by the engine. Tried on copies so the real game is untouched.
void checkListedActionsAreAccepted(const GameState& state, const std::mt19937& rng, Stats& st) {
  const LegalActions legal = getLegalActions(state);
  for (const Action& l : legal.actions) {
    std::vector<long long> amounts = {l.amount};
    if (l.type == ActionType::AttemptSteal)
      amounts = {legal.minBribe, (legal.minBribe + legal.maxBribe) / 2, legal.maxBribe};
    for (long long amount : amounts) {
      GameState copy = state;
      std::mt19937 r = rng;
      Action a = l;
      a.amount = amount;
      ++st.legalChecked;
      if (!applyAction(copy, a, r)) ++st.legalMismatches;
    }
  }
}

// Fires one random, mostly-nonsense action (wrong player, wrong phase, negative or huge
// amounts, out-of-range tile ids). A rejected action must leave the state bit-for-bit
// unchanged; an accepted one (it can happen by chance) must keep the state consistent.
void injectGarbage(GameState& state, std::mt19937& rng, Stats& st, bool checkEveryStep) {
  const int kActionTypes = static_cast<int>(ActionType::Count);
  Action g;
  g.type = static_cast<ActionType>(rng() % kActionTypes);
  g.playerId = static_cast<int>(rng() % 7) - 1;               // -1 .. 5
  g.amount = static_cast<long long>(rng() % 3000000) - 1000000;  // negative .. huge
  g.tileId = static_cast<int>(rng() % 48) - 4;                // -4 .. 43
  g.targetGrade = static_cast<Grade>(static_cast<int>(rng() % 8) - 1);  // includes invalid grades
  const unsigned long long before = stateDigest(state);
  const bool listed = isListedLegal(getLegalActions(state), g);
  const bool ok = applyAction(state, g, rng);
  ++st.legalChecked;
  if (ok != listed) ++st.legalMismatches;  // completeness: accepted <=> listed
  if (ok) {
    ++st.fuzzAccepted;
  } else {
    ++st.fuzzRejected;
    if (stateDigest(state) != before) ++st.fuzzMutations;
  }
  if (checkEveryStep) {
    const std::string problem = checkInvariants(state);
    if (!problem.empty()) {
      ++st.invariantFailures;
      std::cout << "INVARIANT FAILED after garbage action: " << problem << "\n";
    }
  }
}

int main(int argc, char** argv) {
  int games = 1000;
  unsigned seed = 12345;
  bool verbose = false;
  GameConfig config;
  BotConfig bot;
  int rentMultiplier = 1;
  bool cardsEnabled = true;     // --nocards turns the card tiles into no-ops
  bool fuzz = false;
  bool flatRent = false;        // rent factors 1,2,3,4,5
  double examBonusScale = 1.0;  // 0 disables Exam Season bonuses
  try {
    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      const auto integer = [&](long long lo, long long hi) {
        if (++i >= argc) throw std::runtime_error("missing value for " + arg);
        char* end = nullptr;
        errno = 0;
        const long long v = std::strtoll(argv[i], &end, 10);
        if (errno || end == argv[i] || *end || v < lo || v > hi)
          throw std::runtime_error("invalid integer for " + arg);
        return v;
      };
      const auto number = [&](double lo, double hi) {
        if (++i >= argc) throw std::runtime_error("missing value for " + arg);
        char* end = nullptr;
        errno = 0;
        const double v = std::strtod(argv[i], &end);
        if (errno || end == argv[i] || *end || !std::isfinite(v) || v < lo || v > hi)
          throw std::runtime_error("invalid number for " + arg);
        return v;
      };
      if (arg == "--games") games = static_cast<int>(integer(1, 1000000));
      else if (arg == "--seed") seed = static_cast<unsigned>(integer(0, 4294967295LL));
      else if (arg == "--verbose") verbose = true;
      else if (arg == "--salary") config.salary = integer(0, 1000000000);
      else if (arg == "--cash") config.startCash = integer(0, 1000000000);
      else if (arg == "--orderstep") config.startCashOrderStep = integer(0, 1000000000);
      else if (arg == "--reserve") bot.reserve = integer(0, 1000000000);
      else if (arg == "--upgrade-first") bot.upgradeFirst = true;
      else if (arg == "--highest-grade") bot.highestGrade = true;
      else if (arg == "--rentx") rentMultiplier = static_cast<int>(integer(0, 100));
      else if (arg == "--flatrent") flatRent = true;
      else if (arg == "--nocards") cardsEnabled = false;
      else if (arg == "--fuzz") fuzz = true;
      else if (arg == "--exambonus") examBonusScale = number(0, 1000);
      else if (arg == "--maxtempo") config.maxTempo = static_cast<int>(integer(1, 10000));
      else if (arg == "--rentpct") config.rentPercent = static_cast<int>(integer(0, 10000));
      else if (arg == "--stealprob") g_stealChance = number(0, 1);
      else if (arg == "--help") {
        std::cout << "sim [--games N] [--seed N] [--cash VND] [--salary VND] [--orderstep VND]\n"
                  << "    [--reserve VND] [--upgrade-first] [--highest-grade] [--rentpct P] [--rentx N]\n"
                  << "    [--exambonus SCALE] [--stealprob 0..1] [--maxtempo N]\n"
                  << "    [--flatrent] [--nocards] [--fuzz] [--verbose]\n"
                  << "--cash is the mean starting cash; --orderstep 0 disables compensation.\n";
        return 0;
      } else throw std::runtime_error("unknown option: " + arg);
    }
    if (startingCashForTurnOrder(config, 0) < 0)
      throw std::runtime_error("--cash is too small for --orderstep (first player would start negative)");
  } catch (const std::exception& e) {
    std::cerr << "Configuration error: " << e.what() << ". Use --help.\n";
    return 2;
  }
  if (verbose) games = 1;
  config.cardsEnabled = cardsEnabled;
  if (flatRent)
    for (int i = 0; i < 6; ++i) config.rentFactor[i] = i;
  for (int& f : config.rentFactor) f *= rentMultiplier;
  config.examBonusA = static_cast<long long>(config.examBonusA * examBonusScale);
  config.examBonusAPlus = static_cast<long long>(config.examBonusAPlus * examBonusScale);

  std::cout << "Build: " << __DATE__ << " " << __TIME__ << " | rules v" << kRulesVersion << "\n"
            << "Run: games=" << games << ", seed=" << seed << ", fuzz=" << fuzz << "\n"
            << "Config: mean_cash=" << config.startCash << ", order_step=" << config.startCashOrderStep
            << ", salary=" << config.salary << ", admin_fee=" << config.adminFee
            << ", retake_fee=" << config.retakeFee << ", jail_turns=" << config.jailTurns
            << ", max_doubles=" << config.maxDoublesInRow << ", max_tempo=" << config.maxTempo
            << ", semester=" << config.tempoPerSemester << ", exam_start=" << config.examSeasonStart
            << ", exam_A=" << config.examBonusA << ", exam_Aplus=" << config.examBonusAPlus
            << ", steal_bonus_tempos=" << config.stealBonusTempos << ", cards=" << config.cardsEnabled << "\n"
            << "Initial cash by order: ";
  for (int i = 0; i < 4; ++i) std::cout << (i ? "/" : "") << startingCashForTurnOrder(config, i);
  std::cout << "\nRent factors D/C/B/A/A+: ";
  for (int i = 1; i < 6; ++i) std::cout << (i > 1 ? "/" : "") << config.rentFactor[i];
  std::cout << "; rent_percent=" << config.rentPercent << "; rent at base_rent=10000: ";
  for (int i = 1; i < 6; ++i)
    std::cout << (i > 1 ? "/" : "") << 10000LL * config.rentFactor[i] * config.rentPercent / 100;
  std::cout << "\nBot: reserve=" << bot.reserve << ", upgrade_first=" << bot.upgradeFirst
            << ", highest_grade=" << bot.highestGrade
            << ", steal_probability=" << g_stealChance << "\n"
            << "Invariant checks: every action, including large runs\n";
  if (fuzz) std::cout << "Fuzz run: balance statistics are diagnostic; use a non-fuzz run for comparisons.\n";

  const int kPlayers = 4;
  const long long kActionCap = 200000;  // safety net against infinite loops
  Stats st;
  BalanceMetrics metrics(makeBoard(), config.maxTempo);

  for (int g = 0; g < games; ++g) {
    std::mt19937 rng(seed + static_cast<unsigned>(g));
    GameState state = makeNewGame(kPlayers, makeBoard(), seed + static_cast<unsigned>(g), config);
    state.logEnabled = verbose;
    GameTelemetry telemetry;

    long long actions = 0;
    int midLeader = -1;  // net-worth leader at the halfway tempo
    long long rolls = 0, decisions = 0;
    double initialWorth = 0;
    for (const Player& p : state.players) initialWorth += static_cast<double>(calculateNetWorth(state, p));
    while (state.phase != Phase::GameOver && actions < kActionCap) {
      const int actionTempo = state.currentTempo;
      if (fuzz) {
        injectGarbage(state, rng, st, true);
        ++st.invariantChecks;
        metrics.observe(state, actionTempo, telemetry);
        if (state.phase == Phase::GameOver) break;
        checkListedActionsAreAccepted(state, rng, st);
      }
      const int botActionTempo = state.currentTempo;
      const Action a = botChoose(state, rng, bot);
      if (a.type == ActionType::RollDice) ++rolls;
      else if (getLegalActions(state).actions.size() > 1) ++decisions;
      if (!applyAction(state, a, rng)) {
        ++st.illegalActions;
        if (verbose) std::cout << "ILLEGAL ACTION from bot (phase " << static_cast<int>(state.phase) << ")\n";
        break;
      }
      ++actions;
      metrics.observe(state, botActionTempo, telemetry);
      if (midLeader < 0 && state.currentTempo >= state.config.maxTempo / 2 + 1 && state.phase != Phase::GameOver) {
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
      {
        ++st.invariantChecks;
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
      if (actions >= kActionCap) ++st.hitActionCap;
      continue;
    }
    ++st.completedGames;
    st.startWorthSum += initialWorth;
    metrics.finish(state, telemetry);
    st.totalTempos += state.currentTempo;
    st.totalExpelled += kPlayers - countAlivePlayers(state);
    if (countAlivePlayers(state) < kPlayers) ++st.gamesWithExpulsion;
    for (const Player& p : state.players)
      if (!p.isBankrupt) st.endWorthSum += static_cast<double>(calculateNetWorth(state, p));
    {
      bool hasA = false;
      for (const Tile& t : state.tiles) {
        if (t.ownerId < 0) continue;
        st.endTiles[static_cast<int>(t.grade)] += 1;
        if (t.grade == Grade::A || t.grade == Grade::APlus) hasA = true;
      }
      if (hasA) ++st.gamesWithGradeA;
    }
    st.rollsSum += rolls;
    st.decisionsSum += decisions;
    if (state.currentTempo < state.config.maxTempo) ++st.endedEarly;
    if (midLeader >= 0 && state.winnerId >= 0) {
      ++st.midgameGames;
      if (state.winnerId == midLeader) ++st.midgameLeaderWon;
    }
    if (state.winnerId >= 0) {
      ++st.winsBySeat[state.winnerId];
      ++st.winsByTurnOrder[(state.winnerId - state.firstPlayerIndex + kPlayers) % kPlayers];
      st.winnerWorthSum += static_cast<double>(calculateNetWorth(state, state.players[state.winnerId]));
    }
  }

  if (verbose) return (st.illegalActions || st.invariantFailures || st.hitActionCap || st.fuzzMutations || st.legalMismatches) ? 1 : 0;
  if (!st.completedGames) {
    std::cerr << "No games completed; balance statistics are unavailable.\n";
    return 1;
  }
  const double n = st.completedGames;
  const int maxTempoShown = config.maxTempo;
  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Games simulated:        " << st.games << "\n";
  std::cout << "Games completed:        " << st.completedGames << "\n";
  std::cout << "Incomplete games:       " << st.games - st.completedGames << "\n";
  std::cout << "Illegal bot actions:    " << st.illegalActions << "\n";
  std::cout << "Invariant failures:     " << st.invariantFailures << "\n";
  std::cout << "Invariant checks run:   " << st.invariantChecks << "\n";
  std::cout << "Games hitting the cap:  " << st.hitActionCap << "\n";
  std::cout << "Avg actions per attempt:" << " " << double(st.totalActions) / st.games << "\n";
  std::cout << "Avg final tempo:        " << st.totalTempos / n << "\n";
  std::cout << "Avg players expelled:   " << st.totalExpelled / n << " / " << kPlayers << "\n";
  std::cout << "Games with an expulsion:" << " " << 100.0 * st.gamesWithExpulsion / n << "%\n";
  std::cout << "Net worth growth:       ";
  if (st.startWorthSum > 0) std::cout << "x" << st.endWorthSum / st.startWorthSum;
  else std::cout << "n/a (zero starting worth)";
  std::cout << " (cash + invested asset value; not cash supply)\n";
  std::cout << "Owned tiles at the end: D " << st.endTiles[1] / n << ", C " << st.endTiles[2] / n
            << ", B " << st.endTiles[3] / n << ", A " << st.endTiles[4] / n
            << ", A+ " << st.endTiles[5] / n << "  (average per game; "
            << 100.0 * st.gamesWithGradeA / n << "% of games end with an A/A+ tile)\n";
  std::cout << "Per game:               " << st.rollsSum / n << " rolls, " << st.decisionsSum / n
            << " real decisions\n";
  std::cout << "Ended before tempo " << maxTempoShown << ":  " << 100.0 * st.endedEarly / n << "%\n";
  std::cout << "Avg winner net worth:   " << st.winnerWorthSum / n << "\n";
  if (st.midgameGames > 0)
    std::cout << "Midgame leader wins:    " << 100.0 * st.midgameLeaderWon / st.midgameGames
              << "%  (of " << st.midgameGames << " games reaching the halfway tempo)\n";
  std::cout << "Win rate by seat:       ";
  for (int i = 0; i < kPlayers; ++i)
    std::cout << "P" << (i + 1) << " " << 100.0 * st.winsBySeat[i] / n << "%  ";
  std::cout << "\nWin rate by turn order: ";
  for (int i = 0; i < kPlayers; ++i)
    std::cout << (i + 1) << (i == 0 ? "st " : i == 1 ? "nd " : i == 2 ? "rd " : "th ")
              << 100.0 * st.winsByTurnOrder[i] / n << "%  ";
  std::cout << "\n";
  const auto extremes = std::minmax_element(st.winsByTurnOrder.begin(), st.winsByTurnOrder.end());
  const double orderGap = 100.0 * (*extremes.second - *extremes.first) / n;
  std::cout << "Turn-order gap:         " << orderGap << " percentage points ("
            << (orderGap <= 3 ? "within provisional target" : orderGap <= 6 ? "review" : "large advantage") << ")\n";
  std::cout << "Win-rate uncertainty:   approx +/- " << 100 * 1.96 * std::sqrt(0.25 * 0.75 / n)
            << " percentage points per order near 25% (95%, not a simultaneous interval)\n";
  if (st.completedGames < 10000) std::cout << "Sample note: screening only; confirm fairness with >=10000 games and independent seeds.\n";
  metrics.print(std::cout, st.completedGames, st.startWorthSum);
  if (fuzz) {
    std::cout << "Fuzz: garbage actions rejected " << st.fuzzRejected << ", accepted by chance "
              << st.fuzzAccepted << ", rejected-but-changed-state " << st.fuzzMutations << "\n";
    std::cout << "Fuzz: getLegalActions vs applyAction: " << st.legalChecked << " comparisons, "
              << st.legalMismatches << " mismatches\n";
  }
  return (st.illegalActions || st.invariantFailures || st.hitActionCap || st.fuzzMutations ||
          st.legalMismatches) ? 1 : 0;
}
