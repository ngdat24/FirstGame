// Hotseat text client. It talks to the engine ONLY through JSON text (protocol.h), exactly
// like a Godot UI will, so it proves the protocol is complete: if this client can play a
// whole game, so can a UI.
//
//   g++ -std=c++17 -O2 play.cpp protocol.cpp GameLogic.cpp -o play
//   ./play                 4 humans share the keyboard
//   ./play --seed 7        reproducible game (dev only)
//   ./play --auto          random legal moves for everyone, prints the whole game
//
// On Windows consoles run `chcp 65001` first so the Vietnamese card texts display correctly.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "protocol.h"
#include "third_party/json.hpp"

using json = nlohmann::json;

namespace {

std::string money(long long v) {
  std::string s = std::to_string(v < 0 ? -v : v);
  for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
  return (v < 0 ? "-" : "") + s;
}

json ask(Engine& engine, const json& request) {
  return json::parse(handleRequest(engine, request.dump()));
}

std::string nextGrade(const std::string& g) {
  if (g == "D") return "C";
  if (g == "C") return "B";
  return "A";
}

std::string playerName(const json& snap, int id) {
  return id >= 0 ? snap["players"][static_cast<size_t>(id)]["name"].get<std::string>() : "the bank";
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void printBoard(const json& snap) {
  std::cout << "\n--- Board ---\n";
  for (const json& t : snap["tiles"]) {
    std::cout << " #" << t["id"].get<int>() << "\t" << t["name"].get<std::string>();
    if (t["type"] == "institute") {
      std::cout << "  [price " << money(t["base_price"]) << "]";
      if (t["owner_id"].get<int>() >= 0)
        std::cout << "  owner " << playerName(snap, t["owner_id"]) << ", grade "
                  << t["grade"].get<std::string>() << ", rent " << money(t["rent"]);
    }
    std::cout << "\n";
  }
}

void printState(const json& snap, const json& events) {
  std::cout << "\n==================== Tempo " << snap["tempo"].get<int>() << "/"
            << snap["max_tempo"].get<int>() << " ====================\n";
  for (const json& e : events) std::cout << "  * " << e.get<std::string>() << "\n";
  if (snap["rent_free_turns_left"].get<int>() > 0)
    std::cout << "  (Rent holiday is active: nobody pays rent)\n";
  std::cout << "\n";
  const int current = snap["current_player_id"];
  for (const json& p : snap["players"]) {
    const int id = p["id"];
    std::cout << (id == current && !snap["game_over"].get<bool>() ? " > " : "   ") << "["
              << id + 1 << "] " << p["name"].get<std::string>();
    if (p["is_bankrupt"].get<bool>()) {
      std::cout << "  EXPELLED\n";
      continue;
    }
    std::cout << "  cash " << money(p["cash"]) << "  net worth " << money(p["net_worth"])
              << "  at #" << p["position"].get<int>() << "  tiles:";
    for (const json& tid : p["owned_tile_ids"]) {
      const json& t = snap["tiles"][tid.get<size_t>()];
      std::cout << " #" << tid.get<int>() << "(" << t["grade"].get<std::string>() << ")";
    }
    if (p["owned_tile_ids"].empty()) std::cout << " none";
    if (p["jail_turns"].get<int>() > 0) std::cout << "  [JAIL " << p["jail_turns"].get<int>() << "]";
    if (p["skip_turns"].get<int>() > 0) std::cout << "  [skips next turn]";
    if (p["halve_next_dice"].get<bool>()) std::cout << "  [next move halved]";
    if (p["steal_bonus_percent"].get<int>() > 0)
      std::cout << "  [+" << p["steal_bonus_percent"].get<int>() << "% steal]";
    std::cout << "\n";
  }
  const json& dice = snap["last_dice"];
  if (dice[0].get<int>() > 0)
    std::cout << "\n Last roll: " << dice[0].get<int>() << " + " << dice[1].get<int>() << "\n";
}

std::string describe(const json& act, const json& snap, const json& rules) {
  const std::string type = act["type"];
  const int pending = snap["pending_tile_id"];
  const json* pt = pending >= 0 ? &snap["tiles"][static_cast<size_t>(pending)] : nullptr;
  auto tileOf = [&](const json& a) -> const json& {
    return snap["tiles"][a["tile_id"].get<size_t>()];
  };

  if (type == "roll_dice") return "Roll the dice";
  if (type == "pay_retake_fee")
    return "Pay " + money(rules["retake_fee"]) + " to leave the Retake Exam room, then roll";
  if (type == "buy" && pt)
    return "Buy " + (*pt)["name"].get<std::string>() + " for " + money((*pt)["base_price"]);
  if (type == "upgrade" && pt)
    return "Upgrade " + (*pt)["name"].get<std::string>() + " (" +
           (*pt)["grade"].get<std::string>() + " -> " + nextGrade((*pt)["grade"]) + ") for " +
           money((*pt)["upgrade_cost"]);
  if (type == "decline") return "Do nothing";
  if (type == "pay_rent" && pt) {
    if (snap["rent_free_turns_left"].get<int>() > 0) return "Pay no rent (rent holiday)";
    return "Pay rent " + money((*pt)["rent"]) + " to " + playerName(snap, (*pt)["owner_id"]);
  }
  if (type == "attempt_steal" && pt)
    return "Try to steal it with a bribe (" + money(snap["legal"]["min_bribe"]) + " to " +
           money(snap["legal"]["max_bribe"]) + "); chance = bribe / " +
           money((*pt)["total_invested"]) + ", lost if you fail";
  if (type == "teleport_unowned")
    return "Teleport to " + tileOf(act)["name"].get<std::string>() + " and buy it for " +
           money(tileOf(act)["base_price"]) + " (+" + money(rules["admin_fee"]) + " fee)";
  if (type == "teleport_owned")
    return "Teleport to " + tileOf(act)["name"].get<std::string>() + " and upgrade it for " +
           money(tileOf(act)["upgrade_cost"]) + " (+" + money(rules["admin_fee"]) + " fee)";
  if (type == "sell_tile")
    return "Sell " + tileOf(act)["name"].get<std::string>() + " to the bank for " +
           money(tileOf(act)["sell_value"]);
  return type;
}

void printGameOver(const json& snap) {
  std::cout << "\n############ GAME OVER ############\n";
  std::vector<json> ps(snap["players"].begin(), snap["players"].end());
  std::sort(ps.begin(), ps.end(), [](const json& a, const json& b) {
    return a["net_worth"].get<long long>() > b["net_worth"].get<long long>();
  });
  for (const json& p : ps)
    std::cout << " " << p["name"].get<std::string>() << (p["is_bankrupt"].get<bool>() ? " (expelled)" : "")
              << ": net worth " << money(p["net_worth"]) << "\n";
  std::cout << " Winner: " << playerName(snap, snap["winner_id"]) << "\n";
}

bool readLine(std::string& line) { return static_cast<bool>(std::getline(std::cin, line)); }

}  // namespace

int main(int argc, char** argv) {
  bool autoPlay = false;
  long long seed = -1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--auto") autoPlay = true;
    else if (arg == "--seed" && i + 1 < argc) seed = std::atoll(argv[++i]);
  }

  Engine engine;
  engine.allowSeed = (seed >= 0);
  std::mt19937 chooser(seed >= 0 ? static_cast<unsigned>(seed) + 1 : std::random_device{}());

  const json rules = ask(engine, {{"cmd", "get_rules"}})["rules"];
  json newGame = {{"cmd", "new_game"}};
  if (seed >= 0) newGame["seed"] = seed;
  json resp = ask(engine, newGame);
  if (!resp["ok"].get<bool>()) {
    std::cerr << "could not start a game: " << resp["message"].get<std::string>() << "\n";
    return 1;
  }
  if (!autoPlay)
    std::cout << "HUST Monopoly (hotseat). Type a number to choose, b = board, q = quit.\n";

  while (true) {
    const json snap = resp["snapshot"];
    printState(snap, resp["events"]);
    if (snap["game_over"].get<bool>()) {
      printGameOver(snap);
      return 0;
    }

    const json& actions = snap["legal"]["actions"];
    const int who = snap["current_player_id"];
    json chosen;
    while (chosen.is_null()) {
      std::cout << "\n " << playerName(snap, who) << ", your options:\n";
      for (size_t i = 0; i < actions.size(); ++i)
        std::cout << "  " << i + 1 << ") " << describe(actions[i], snap, rules) << "\n";

      size_t index = 0;
      if (autoPlay) {
        index = std::uniform_int_distribution<size_t>(0, actions.size() - 1)(chooser);
        std::cout << "  -> auto picks " << index + 1 << "\n";
      } else {
        std::cout << " Choose: ";
        std::string line;
        if (!readLine(line)) return 0;
        if (line == "q") return 0;
        if (line == "b") {
          printBoard(snap);
          continue;
        }
        const long n = std::strtol(line.c_str(), nullptr, 10);
        if (n < 1 || n > static_cast<long>(actions.size())) {
          std::cout << " Please type a number from 1 to " << actions.size() << ".\n";
          continue;
        }
        index = static_cast<size_t>(n - 1);
      }

      chosen = actions[index];
      if (chosen["type"] == "attempt_steal") {
        const long long lo = snap["legal"]["min_bribe"], hi = snap["legal"]["max_bribe"];
        long long bribe = lo;
        if (autoPlay) {
          bribe = std::uniform_int_distribution<long long>(lo, hi)(chooser);
        } else {
          std::cout << " Bribe amount (" << money(lo) << " - " << money(hi) << "): ";
          std::string line;
          if (!readLine(line)) return 0;
          bribe = std::atoll(line.c_str());
          if (bribe < lo || bribe > hi) {
            std::cout << " That is not between " << money(lo) << " and " << money(hi) << ".\n";
            chosen = nullptr;
            continue;
          }
          const json& t = snap["tiles"][snap["pending_tile_id"].get<size_t>()];
          const double pct =
              std::min(100.0, 100.0 * static_cast<double>(bribe) /
                                  std::max<long long>(1, t["total_invested"].get<long long>()) +
                                  snap["players"][static_cast<size_t>(who)]["steal_bonus_percent"].get<int>());
          std::cout << " Chance of success: " << pct << "%\n";
        }
        chosen["amount"] = bribe;
      }
    }

    json request = {{"cmd", "action"}, {"type", chosen["type"]}, {"player_id", chosen["player_id"]},
                    {"tile_id", chosen["tile_id"]}, {"amount", chosen["amount"]}};
    resp = ask(engine, request);
    if (!resp["ok"].get<bool>()) {  // cannot happen with legal actions; kept for safety
      std::cerr << "engine refused: " << resp["message"].get<std::string>() << "\n";
      return 1;
    }
  }
}
