// Tests for the JSON protocol layer (protocol.cpp). Build & run:
//   g++ -std=c++17 -Wall -Wextra -O2 tests_protocol.cpp protocol.cpp GameLogic.cpp -o tests_protocol
//   ./tests_protocol
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "board_data.h"
#include "protocol.h"
#include "third_party/json.hpp"

using json = nlohmann::json;

// --quick shrinks the long loops (useful under sanitizers, which are ~50x slower).
static unsigned g_games = 40;
static int g_fuzzRounds = 20000;

static int g_total = 0;
static int g_failed = 0;

#define CHECK(cond)                                                  \
  do {                                                               \
    ++g_total;                                                       \
    if (!(cond)) {                                                   \
      ++g_failed;                                                    \
      std::cout << "FAIL (line " << __LINE__ << "): " #cond << "\n"; \
    }                                                                \
  } while (0)

static json call(Engine& e, const std::string& request) {
  return json::parse(handleRequest(e, request));  // the response must always be valid JSON
}
static json call(Engine& e, const char* request) { return call(e, std::string(request)); }
static json call(Engine& e, const json& request) { return call(e, request.dump()); }

static Engine devEngine() {
  Engine e;
  e.allowSeed = true;
  return e;
}

static bool isError(const json& r, const char* code) {
  return r.is_object() && r.value("ok", true) == false && r.value("error", "") == code;
}

static void testRulesAndNewGame() {
  Engine e;
  const json rules = call(e, R"({"cmd":"get_rules"})");
  CHECK(rules["ok"] == true);
  CHECK(rules["cards"].size() == 11);
  CHECK(rules["rules"]["start_cash"] == 700000);
  CHECK(rules["rules"]["rent_factor"]["A"] == 16);

  CHECK(isError(call(e, R"({"cmd":"get_state"})"), "no_game"));
  CHECK(isError(call(e, R"({"cmd":"action","type":"roll_dice","player_id":0})"), "no_game"));

  const json g = call(e, R"({"cmd":"new_game"})");
  CHECK(g["ok"] == true);
  const json& snap = g["snapshot"];
  CHECK(snap["players"].size() == 4 && snap["tiles"].size() == 40);
  CHECK(snap["phase"] == "await_roll");
  CHECK(snap["tempo"] == 1);
  CHECK(snap["current_player_id"] == snap["first_player_id"]);
  CHECK(snap["legal"]["actions"].size() == 1);
  CHECK(snap["legal"]["actions"][0]["type"] == "roll_dice");
  CHECK(g["events"].size() == 1);
  CHECK(call(e, R"({"cmd":"get_state"})")["snapshot"] == snap);

  // Only 4 players; seeds are a dev-only feature and must be integers in range.
  CHECK(isError(call(e, R"({"cmd":"new_game","players":3})"), "unsupported_players"));
  CHECK(isError(call(e, R"({"cmd":"new_game","players":"4"})"), "bad_request"));
  CHECK(call(e, R"({"cmd":"new_game","players":4})")["ok"] == true);
  CHECK(isError(call(e, R"({"cmd":"new_game","seed":5})"), "seed_not_allowed"));

  Engine d = devEngine();
  CHECK(call(d, R"({"cmd":"new_game","seed":5})")["ok"] == true);
  CHECK(isError(call(d, R"({"cmd":"new_game","seed":"x"})"), "bad_request"));
  CHECK(isError(call(d, R"({"cmd":"new_game","seed":1.5})"), "bad_request"));
  CHECK(isError(call(d, R"({"cmd":"new_game","seed":-1})"), "bad_request"));
  CHECK(isError(call(d, R"({"cmd":"new_game","seed":4294967296})"), "bad_request"));
}

static void testHiddenState() {
  // Two states that differ ONLY in the hidden parts must produce identical snapshots.
  GameState a = makeNewGame(4, makeBoard(), 1);
  GameState b = a;
  a.deck.drawPile = {0, 1, 2, 3};
  b.deck.drawPile = {3, 2, 1, 0};
  b.deckRng.seed(999);
  CHECK(snapshotToJson(a) == snapshotToJson(b));

  // The snapshot exposes the NUMBER of cards left but nothing that names them.
  const json s = json::parse(snapshotToJson(a));
  CHECK(s["deck"].size() == 2);
  CHECK(s["deck"]["draw_pile_count"] == 4);
  const std::string text = snapshotToJson(a);
  CHECK(text.find("rng") == std::string::npos);
  CHECK(text.find("draw_pile\"") == std::string::npos);  // only draw_pile_count exists
  CHECK(text.find("title") == std::string::npos);        // no card texts at all

  // Played cards are public (ids on the discard pile).
  a.deck.discardPile = {4, 7};
  const json t = json::parse(snapshotToJson(a));
  CHECK(t["deck"]["discard_pile"] == json::array({5, 8}));  // card ids are index + 1
}

static void testBadRequests() {
  Engine e = devEngine();
  CHECK(call(e, R"({"cmd":"new_game","seed":1})")["ok"] == true);
  const std::string before = snapshotToJson(e.state);

  const std::vector<std::string> junk = {
      "",           "   ",         "not json",   "[]",        "42",
      "null",       "\"cmd\"",     "{}",         R"({"cmd":5})",
      R"({"cmd":null})", R"({"cmd":"nope"})",    R"({"cmd":"action"})",
      R"({"cmd":"action","type":5,"player_id":0})",
      R"({"cmd":"action","type":"fly","player_id":0})",
      R"({"cmd":"action","type":"roll_dice"})",
      R"({"cmd":"action","type":"roll_dice","player_id":"0"})",
      R"({"cmd":"action","type":"roll_dice","player_id":null})",
      R"({"cmd":"action","type":"roll_dice","player_id":0.5})",
      R"({"cmd":"action","type":"roll_dice","player_id":99999999999})",
      R"({"cmd":"action","type":"attempt_steal","player_id":0,"amount":1.5})",
      R"({"cmd":"action","type":"attempt_steal","player_id":0,"amount":1e3})",
      R"({"cmd":"action","type":"attempt_steal","player_id":0,"amount":1e300})",
      R"({"cmd":"action","type":"attempt_steal","player_id":0,"amount":"5"})",
      R"({"cmd":"action","type":"attempt_steal","player_id":0,"amount":18446744073709551615})",
      R"({"cmd":"action","type":"sell_tile","player_id":0,"tile_id":[1]})",
      R"({"cmd":"action","type":"roll_dice","player_id":0)",  // truncated
      std::string(5000, ' '),                                  // too large
      std::string(2000, '[') + std::string(2000, ']'),         // deeply nested
      std::string("{\"cmd\":\"get_state\"}\0{", 20),           // embedded NUL
  };
  for (const std::string& request : junk) {
    const json r = call(e, request);
    CHECK(r.is_object() && r.value("ok", true) == false);
    CHECK(snapshotToJson(e.state) == before);  // rejected requests never change the game
  }

  // Well-formed but illegal: wrong player, wrong phase, nonsense targets.
  CHECK(isError(call(e, R"({"cmd":"action","type":"roll_dice","player_id":99})"), "illegal_action"));
  const int first = e.state.currentPlayerIndex;
  const json wrongPlayer = {{"cmd", "action"}, {"type", "roll_dice"}, {"player_id", (first + 1) % 4}};
  CHECK(isError(call(e, wrongPlayer), "illegal_action"));
  const json wrongPhase = {{"cmd", "action"}, {"type", "buy"}, {"player_id", first}};
  CHECK(isError(call(e, wrongPhase), "illegal_action"));
  const json wrongPhase2 = {{"cmd", "action"}, {"type", "sell_tile"}, {"player_id", first}, {"tile_id", 3}};
  CHECK(isError(call(e, wrongPhase2), "illegal_action"));
  CHECK(snapshotToJson(e.state) == before);

  // The one legal action works, and the game moves on.
  const json roll = {{"cmd", "action"}, {"type", "roll_dice"}, {"player_id", first}};
  const json r = call(e, roll);
  CHECK(r["ok"] == true);
  CHECK(r["events"].size() >= 1);
  CHECK(snapshotToJson(e.state) != before);
}

// A client that sees ONLY the JSON snapshot picks random legal actions until the game ends.
// Returns the final snapshot text; counts problems through CHECK.
static std::string playOneGame(Engine& e, unsigned gameSeed, unsigned chooserSeed) {
  std::mt19937 chooser(chooserSeed);
  json resp = call(e, json{{"cmd", "new_game"}, {"seed", gameSeed}});
  CHECK(resp["ok"] == true);
  for (int step = 0; step < 50000; ++step) {
    const json& snap = resp["snapshot"];
    if (snap["game_over"] == true) {
      CHECK(snap["winner_id"].get<int>() >= 0 && snap["winner_id"].get<int>() < 4);
      CHECK(snap["legal"]["actions"].empty());
      return snap.dump();
    }
    const json& actions = snap["legal"]["actions"];
    CHECK(!actions.empty());
    if (actions.empty()) return "";
    for (const json& a : actions) CHECK(a["player_id"] == snap["current_player_id"]);

    json pick = actions[std::uniform_int_distribution<size_t>(0, actions.size() - 1)(chooser)];
    if (pick["type"] == "attempt_steal") {
      const long long lo = snap["legal"]["min_bribe"], hi = snap["legal"]["max_bribe"];
      CHECK(lo >= 1 && hi >= lo);
      pick["amount"] = std::uniform_int_distribution<long long>(lo, hi)(chooser);
    }
    json request = pick;
    request["cmd"] = "action";
    resp = call(e, request);
    CHECK(resp["ok"] == true);
    if (resp["ok"] != true) return "";
  }
  CHECK(false);  // the game never ended
  return "";
}

static void testJsonClientsFinishGames() {
  for (unsigned seed = 1; seed <= g_games; ++seed) {
    Engine e = devEngine();
    playOneGame(e, seed, seed * 7919u);
  }
}

static void testDeterministicReplay() {
  Engine a = devEngine(), b = devEngine(), c = devEngine();
  const std::string ra = playOneGame(a, 42, 1234);
  const std::string rb = playOneGame(b, 42, 1234);
  const std::string rc = playOneGame(c, 43, 1234);
  CHECK(!ra.empty());
  CHECK(ra == rb);  // same seed, same choices -> identical game
  CHECK(ra != rc);  // a different seed gives a different game
}

static void testEndpointFuzz() {
  // Random garbage and randomly corrupted valid requests, mid-game. The engine must never
  // crash, always answer with valid JSON, and never change the state when it says no.
  Engine e = devEngine();
  CHECK(call(e, R"({"cmd":"new_game","seed":9})")["ok"] == true);
  std::mt19937 rng(2024);
  const std::vector<std::string> templates = {
      R"({"cmd":"action","type":"roll_dice","player_id":0})",
      R"({"cmd":"action","type":"buy","player_id":1,"tile_id":3,"amount":0})",
      R"({"cmd":"action","type":"attempt_steal","player_id":2,"amount":12345})",
      R"({"cmd":"action","type":"teleport_unowned","player_id":3,"tile_id":7})",
      R"({"cmd":"get_state"})",
  };
  long long accepted = 0, rejected = 0;
  for (int i = 0; i < g_fuzzRounds; ++i) {
    std::string request = templates[rng() % templates.size()];
    const int mutations = static_cast<int>(rng() % 4);
    for (int m = 0; m < mutations && !request.empty(); ++m) {
      const size_t pos = rng() % request.size();
      switch (rng() % 3) {
        case 0: request[pos] = static_cast<char>(rng() % 256); break;
        case 1: request.erase(pos, 1); break;
        default: request.insert(pos, 1, "{}[]\",:0123456789-eE."[rng() % 22]); break;
      }
    }
    if (rng() % 10 == 0) {  // sometimes pure noise
      request.assign(rng() % 64, ' ');
      for (char& ch : request) ch = static_cast<char>(rng() % 256);
    }
    const std::string before = snapshotToJson(e.state);
    const json r = json::parse(handleRequest(e, request));
    CHECK(r.is_object() && r.contains("ok"));
    if (r["ok"] == true) {
      ++accepted;
    } else {
      ++rejected;
      CHECK(snapshotToJson(e.state) == before);
    }
    if (e.state.phase == Phase::GameOver) CHECK(call(e, R"({"cmd":"new_game","seed":9})")["ok"] == true);
  }
  CHECK(rejected > 0);
  std::cout << "endpoint fuzz: " << rejected << " rejected, " << accepted
            << " accepted (valid by chance or read-only)\n";
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--quick") {
    g_games = 4;
    g_fuzzRounds = 1500;
  }
  testRulesAndNewGame();
  testHiddenState();
  testBadRequests();
  testJsonClientsFinishGames();
  testDeterministicReplay();
  testEndpointFuzz();
  std::cout << (g_total - g_failed) << "/" << g_total << " checks passed\n";
  return g_failed == 0 ? 0 : 1;
}
