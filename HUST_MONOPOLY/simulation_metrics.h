#pragma once

#include <algorithm>
#include <array>
#include <iomanip>
#include <ostream>
#include <vector>

#include "GameLogic.h"

// Diagnostic observations only: these functions never consume RNG or alter the game.
struct EconomyCheckpoint {
  int endTempo;
  int games = 0;
  long long livingPlayers = 0;
  long long lowCashPlayers = 0;
  std::vector<long long> cash;
  std::array<double, 4> ownedByOrder{};
  double leaderShareSum = 0;
};

struct GameTelemetry {
  int firstExpulsionTempo = 0;
  int firstATempo = 0;
  int firstAPlusTempo = 0;
  std::vector<bool> captured;
};

struct BalanceMetrics {
  long long referenceUpgradeCost = 0;
  int earlyTempo;
  int gamesWithExpulsion = 0;
  int earlyExpulsions = 0;
  long long firstExpulsionTempoSum = 0;
  int gamesWithA = 0, gamesWithAPlus = 0;
  long long firstATempoSum = 0, firstAPlusTempoSum = 0;
  double finalCashSum = 0;
  double finalLeaderShareSum = 0;
  std::vector<EconomyCheckpoint> checkpoints;

  BalanceMetrics(const std::vector<Tile>& board, int maxTempo) : earlyTempo(std::max(1, maxTempo / 4)) {
    std::vector<long long> costs;
    for (const Tile& t : board)
      if (t.type == TileType::Institute) costs.push_back(t.upgradeCost);
    std::sort(costs.begin(), costs.end());
    if (!costs.empty()) referenceUpgradeCost = (costs[(costs.size() - 1) / 2] + costs[costs.size() / 2]) / 2;
    for (int tempo : {earlyTempo, std::max(1, maxTempo / 2), std::max(1, 3 * maxTempo / 4), maxTempo})
      if (checkpoints.empty() || checkpoints.back().endTempo != tempo)
        checkpoints.push_back(EconomyCheckpoint{tempo, 0, 0, 0, {}, {}, 0});
  }

  void observe(const GameState& s, int actionTempo, GameTelemetry& game) {
    if (!game.firstExpulsionTempo && countAlivePlayers(s) < 4) game.firstExpulsionTempo = actionTempo;
    for (const Tile& t : s.tiles) {
      if (t.ownerId < 0) continue;
      if (!game.firstATempo && (t.grade == Grade::A || t.grade == Grade::APlus)) game.firstATempo = actionTempo;
      if (!game.firstAPlusTempo && t.grade == Grade::APlus) game.firstAPlusTempo = actionTempo;
    }
    if (game.captured.empty()) game.captured.resize(checkpoints.size(), false);
    for (size_t i = 0; i < checkpoints.size(); ++i) {
      auto& cp = checkpoints[i];
      if (game.captured[i] || !(s.currentTempo > cp.endTempo ||
          (s.phase == Phase::GameOver && s.currentTempo >= cp.endTempo))) continue;
      game.captured[i] = true;
      ++cp.games;
      long long totalWorth = 0, largestWorth = 0;
      for (const Player& p : s.players) {
        const int order = (p.id - s.firstPlayerIndex + 4) % 4;
        for (int tid : p.ownedTileIds)
          if (s.tiles[tid].type == TileType::Institute) ++cp.ownedByOrder[order];
        if (p.isBankrupt) continue;
        ++cp.livingPlayers;
        cp.cash.push_back(p.cash);
        if (p.cash < referenceUpgradeCost) ++cp.lowCashPlayers;
        const long long worth = calculateNetWorth(s, p);
        totalWorth += worth;
        largestWorth = std::max(largestWorth, worth);
      }
      if (totalWorth > 0) cp.leaderShareSum += double(largestWorth) / totalWorth;
    }
  }

  void finish(const GameState& s, const GameTelemetry& game) {
    if (game.firstExpulsionTempo) {
      ++gamesWithExpulsion;
      firstExpulsionTempoSum += game.firstExpulsionTempo;
      if (game.firstExpulsionTempo <= earlyTempo) ++earlyExpulsions;
    }
    if (game.firstATempo) { ++gamesWithA; firstATempoSum += game.firstATempo; }
    if (game.firstAPlusTempo) { ++gamesWithAPlus; firstAPlusTempoSum += game.firstAPlusTempo; }
    long long totalWorth = 0, largestWorth = 0;
    for (const Player& p : s.players) {
      finalCashSum += p.cash;
      if (p.isBankrupt) continue;
      const long long worth = calculateNetWorth(s, p);
      totalWorth += worth;
      largestWorth = std::max(largestWorth, worth);
    }
    if (totalWorth > 0) finalLeaderShareSum += double(largestWorth) / totalWorth;
  }

  void print(std::ostream& out, int completed, double initialCashSum) {
    out << std::fixed << std::setprecision(2);
    out << "Cash supply growth:     ";
    if (initialCashSum > 0) out << "x" << finalCashSum / initialCashSum;
    else out << "n/a (zero starting cash)";
    out << " (cash only, separate from net worth)\n";
    out << "Final leader share:     " << (completed ? 100 * finalLeaderShareSum / completed : 0) << "% of net worth\n";
    out << "First expulsion by tempo " << earlyTempo << ": "
        << (completed ? 100.0 * earlyExpulsions / completed : 0) << "% of completed games\n";
    out << "Avg first expulsion:    ";
    if (gamesWithExpulsion) out << "tempo " << double(firstExpulsionTempoSum) / gamesWithExpulsion;
    else out << "n/a";
    out << " (" << gamesWithExpulsion << " games)\n";
    out << "First grade A:          ";
    if (gamesWithA) out << "tempo " << double(firstATempoSum) / gamesWithA;
    else out << "n/a";
    out << " (" << gamesWithA << " games ever reaching A)\n";
    out << "First grade A+:         ";
    if (gamesWithAPlus) out << "tempo " << double(firstAPlusTempoSum) / gamesWithAPlus;
    else out << "n/a";
    out << " (" << gamesWithAPlus << " games ever reaching A+)\n";
    out << "Liquidity reference:    " << referenceUpgradeCost << " VND (median board upgrade cost)\n";
    for (auto& cp : checkpoints) {
      out << "After tempo " << cp.endTempo << ": ";
      if (!cp.games) { out << "n/a (no games reached this checkpoint)\n"; continue; }
      std::sort(cp.cash.begin(), cp.cash.end());
      const double median = cp.cash.empty() ? 0 :
          (double(cp.cash[(cp.cash.size() - 1) / 2]) + cp.cash[cp.cash.size() / 2]) / 2;
      out << "games=" << cp.games << ", living=" << double(cp.livingPlayers) / cp.games
          << ", median_cash=" << median << ", below_upgrade="
          << (cp.livingPlayers ? 100.0 * cp.lowCashPlayers / cp.livingPlayers : 0)
          << "%, leader_share=" << 100 * cp.leaderShareSum / cp.games << "%, owned_by_order=";
      for (int i = 0; i < 4; ++i) out << (i ? "/" : "") << cp.ownedByOrder[i] / cp.games;
      out << "\n";
    }
  }
};
