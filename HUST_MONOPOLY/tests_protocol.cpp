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
  const GameConfig defaults;
  CHECK(rules["rules"]["start_cash"] == defaults.startCash);
  CHECK(rules["rules"]["rules_version"] == kRulesVersion);
  CHECK(rules["rules"]["start_cash_order_step"] == 0);
  CHECK(rules["rules"]["starting_cash_by_turn_order"] == json::array({1000000, 1000000, 1000000, 1000000}));
  CHECK(rules["rules"]["rent_percent"] == 100);
  CHECK(rules["rules"]["steal_bonus_tempos"] == 3);
  CHECK(rules["rules"]["rent_factor"]["A"] == defaults.rentFactor[4]);
  CHECK(rules["rules"]["max_tempo"] == defaults.maxTempo);
  CHECK(rules["rules"]["salary"] == defaults.salary);

  CHECK(isError(call(e, R"({"cmd":"get_state"})"), "no_game"));
  CHECK(isError(call(e, R"({"cmd":"action","type":"roll_dice","player_id":0})"), "no_game"));

  const json g = call(e, R"({"cmd":"new_game"})");
  CHECK(g["ok"] == true);
  const json& snap = g["snapshot"];
  CHECK(snap["players"].size() == 4 && snap["tiles"].size() == 40);
  CHECK(snap["phase"] == "await_roll");
  CHECK(snap["tempo"] == 1);
  CHECK(snap["current_player_id"] == snap["first_player_id"]);
  long long totalCash = 0;
  const int first = snap["first_player_id"];
  for (int order = 0; order < 4; ++order) {
    const auto& player = snap["players"][(first + order) % 4];
    CHECK(player["cash"] == rules["rules"]["starting_cash_by_turn_order"][order]);
    totalCash += player["cash"].get<long long>();
  }
  CHECK(totalCash == 4000000);
  CHECK(snap["legal"]["actions"].size() == 1);
  CHECK(snap["legal"]["actions"][0]["type"] == "roll_dice");
  CHECK(g["events"].size() == 1);
  CHECK(g["events"][0].get<std::string>().find("Vốn đầu mỗi người: 1000000 VND") != std::string::npos);
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

static Engine landingEngine(Grade grade = Grade::None, int owner = -1) {
  Engine e = devEngine();
  e.state = makeNewGame(4, makeBoard(), 7);
  e.hasGame = true;
  e.state.firstPlayerIndex = e.state.currentPlayerIndex = 0;
  e.state.players[0].position = 3;
  e.state.phase = Phase::AwaitLandingChoice;
  e.state.pendingTileId = 3;
  if (owner >= 0) {
    Tile& t = e.state.tiles[3];
    t.ownerId = owner;
    t.grade = grade;
    t.totalInvested = t.basePrice + (static_cast<int>(grade) - 1) * t.upgradeCost;
    e.state.players[owner].ownedTileIds.push_back(3);
  }
  return e;
}

static void testGradeChoiceProtocol() {
  Engine e = landingEngine();
  const json rules = call(e, {{"cmd", "get_rules"}})["rules"];
  CHECK(rules["purchase_grades"] == json::array({"D", "C", "B"}));
  CHECK(rules["upgrade_targets"]["D"] == json::array({"C", "B"}));
  CHECK(rules["upgrade_targets"]["B"] == json::array({"A"}));
  CHECK(rules["upgrade_targets"]["A"] == json::array({"A+"}));
  CHECK(rules["upgrade_targets"]["A+"].empty());
  const json snap = call(e, {{"cmd", "get_state"}})["snapshot"];
  CHECK(snap["legal"]["actions"].size() == 4);
  const std::vector<std::string> grades = {"D", "C", "B"};
  for (int i = 0; i < 3; ++i) {
    const json choice = snap["legal"]["actions"][i];
    CHECK(choice["target_grade"] == grades[i]);
    CHECK(choice["cost"] == 120000 + i * 60000);
    Engine bought = landingEngine();
    json request = choice;
    request["cmd"] = "action";
    request["cost"] = 0;  // display metadata is not trusted as the actual price
    const json result = call(bought, request);
    CHECK(result["ok"] == true);
    CHECK(result["snapshot"]["tiles"][3]["grade"] == grades[i]);
    CHECK(result["snapshot"]["tiles"][3]["total_invested"] == 120000 + i * 60000);
    CHECK(result["snapshot"]["players"][0]["cash"] == 880000 - i * 60000);
  }
  CHECK(call(e, {{"cmd", "action"}, {"type", "buy"}, {"player_id", 0}})["ok"] == true);
  CHECK(e.state.tiles[3].grade == Grade::D);  // legacy request defaults remain safe

  for (const json& bad : std::vector<json>{nullptr, 3, true, "none", "Z", json::array({"B"}), json::object()}) {
    Engine invalid = landingEngine();
    const std::string before = snapshotToJson(invalid.state);
    const auto beforeRng = invalid.rng;
    CHECK(isError(call(invalid, {{"cmd", "action"}, {"type", "buy"}, {"player_id", 0},
                                {"target_grade", bad}}), "bad_request"));
    CHECK(snapshotToJson(invalid.state) == before && invalid.rng == beforeRng);
  }
  for (const std::string& bad : {std::string("A"), std::string("A+")}) {
    Engine invalid = landingEngine();
    const std::string before = snapshotToJson(invalid.state);
    CHECK(isError(call(invalid, {{"cmd", "action"}, {"type", "buy"}, {"player_id", 0},
                                {"target_grade", bad}}), "illegal_action"));
    CHECK(snapshotToJson(invalid.state) == before);
  }
  Engine poor = landingEngine();
  poor.state.players[0].cash = 179999;
  const std::string poorBefore = snapshotToJson(poor.state);
  CHECK(isError(call(poor, {{"cmd", "action"}, {"type", "buy"}, {"player_id", 0},
                           {"target_grade", "C"}}), "illegal_action"));
  CHECK(snapshotToJson(poor.state) == poorBefore);

  const std::vector<Grade> from = {Grade::D, Grade::C, Grade::B, Grade::A};
  const std::vector<std::string> to = {"B", "B", "A", "A+"};
  for (int i = 0; i < 4; ++i) {
    Engine owned = landingEngine(from[i], 0);
    const int invested = owned.state.tiles[3].totalInvested;
    const json result = call(owned, {{"cmd", "action"}, {"type", "upgrade"}, {"player_id", 0},
                                    {"target_grade", to[i]}});
    const int cost = i == 0 ? 120000 : 60000;
    CHECK(result["ok"] == true);
    CHECK(result["snapshot"]["tiles"][3]["grade"] == to[i]);
    CHECK(result["snapshot"]["tiles"][3]["total_invested"] == invested + cost);
    CHECK(result["snapshot"]["players"][0]["cash"] == 1000000 - cost);
  }
  Engine stolen = landingEngine(Grade::D, 1);
  const json success = call(stolen, {{"cmd", "action"}, {"type", "attempt_steal"},
                                   {"player_id", 0}, {"amount", 180000}});
  CHECK(success["ok"] == true && success["snapshot"]["phase"] == "await_landing_choice");
  CHECK(success["snapshot"]["current_player_id"] == 0);
  CHECK(success["snapshot"]["tiles"][3]["grade"] == "D");
  CHECK(success["snapshot"]["legal"]["actions"][1]["target_grade"] == "B");
  CHECK(success["snapshot"]["legal"]["actions"][1]["cost"] == 120000);
  const json upgraded = call(stolen, {{"cmd", "action"}, {"type", "upgrade"}, {"player_id", 0},
                                    {"target_grade", "B"}});
  CHECK(upgraded["ok"] == true && upgraded["snapshot"]["tiles"][3]["grade"] == "B");
  CHECK(upgraded["snapshot"]["players"][0]["cash"] == 700000);
  CHECK(upgraded["snapshot"]["tiles"][3]["total_invested"] == 240000);
}


static void testAuctionProtocol() {
  Engine e = landingEngine();
  e.state.phase = Phase::AwaitRoll; e.state.pendingTileId = -1;
  e.state.players[0].position = 4;
  resolveLanding(e.state, e.state.players[0], e.state.tiles[4]);
  json snap = call(e, {{"cmd", "get_state"}})["snapshot"];
  CHECK(snap["tiles"][4]["type"] == "auction");
  CHECK(snap["legal"]["actions"][0]["cost"] == 50000);
  CHECK(!snap["legal"]["actions"][0].contains("target_grade"));
  CHECK(snap["players"][0]["auction_count"] == 0);
  const std::string before = snapshotToJson(e.state);
  CHECK(isError(call(e, {{"cmd", "action"}, {"type", "buy"}, {"player_id", 0}, {"target_grade", "D"}}), "illegal_action"));
  CHECK(snapshotToJson(e.state) == before);
  json bought = call(e, {{"cmd", "action"}, {"type", "buy"}, {"player_id", 0}});
  CHECK(bought["ok"] == true);
  CHECK(bought["snapshot"]["tiles"][4]["base_price"] == 70000);
  CHECK(bought["snapshot"]["players"][0]["auction_count"] == 1);
  for (Grade grade : {Grade::C, Grade::B, Grade::A}) {
    Engine stolen = landingEngine(grade, 1);
    const long long invested = stolen.state.tiles[3].totalInvested;
    const json result = call(stolen, {{"cmd", "action"}, {"type", "attempt_steal"},
                                     {"player_id", 0}, {"amount", (3 * invested + 1) / 2}});
    CHECK(result["ok"] == true);
    CHECK(stolen.state.tiles[3].ownerId == 0 && stolen.state.tiles[3].grade == grade);
    if (grade == Grade::A) {
      CHECK(stolen.state.phase == Phase::AwaitRoll && stolen.state.currentPlayerIndex == 1);
      CHECK(stolen.state.pendingTileId == -1);
      for (const auto& a : getLegalActions(stolen.state).actions) CHECK(a.type != ActionType::Upgrade);
    } else CHECK(stolen.state.phase == Phase::AwaitLandingChoice);
  }
}

int main(int argc, char** argv) {
  testAuctionProtocol();
  if (argc > 1 && std::string(argv[1]) == "--quick") {
    g_games = 4;
    g_fuzzRounds = 1500;
  }
  testRulesAndNewGame();
  testGradeChoiceProtocol();
  testHiddenState();
  testBadRequests();
  testJsonClientsFinishGames();
  testDeterministicReplay();
  testEndpointFuzz();
  std::cout << (g_total - g_failed) << "/" << g_total << " checks passed\n";
  return g_failed == 0 ? 0 : 1;
}
