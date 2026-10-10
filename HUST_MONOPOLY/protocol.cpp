#include "protocol.h"

#include <climits>
#include <exception>
#include <vector>

#include "board_data.h"
#include "card_data.h"
#include "third_party/json.hpp"

using json = nlohmann::json;

namespace {

// Requests are tiny; refusing big ones early keeps hostile input cheap to handle.
constexpr size_t kMaxRequestBytes = 4096;
constexpr int kSupportedPlayers = 4;  // the engine and RULES.md are specified for exactly 4

// ---------------------------------------------------------------------------
// Names used on the wire
// ---------------------------------------------------------------------------
const char* phaseName(Phase p) {
  switch (p) {
    case Phase::AwaitRoll: return "await_roll";
    case Phase::AwaitLandingChoice: return "await_landing_choice";
    case Phase::AwaitCornerChoice: return "await_corner_choice";
    case Phase::AwaitLiquidation: return "await_liquidation";
    case Phase::GameOver: return "game_over";
  }
  return "unknown";
}

const char* tileTypeName(TileType t) {
  switch (t) {
    case TileType::Institute: return "institute";
    case TileType::Start: return "start";
    case TileType::SpecialCorner: return "special_corner";
    case TileType::RetakeExam: return "retake_exam";
    case TileType::AcademicWarning: return "academic_warning";
    case TileType::Other: return "card";
  }
  return "unknown";
}

const char* gradeName(Grade g) {
  switch (g) {
    case Grade::None: return "none";
    case Grade::D: return "D";
    case Grade::C: return "C";
    case Grade::B: return "B";
    case Grade::A: return "A";
  }
  return "none";
}

struct ActionName {
  ActionType type;
  const char* name;
};
const ActionName kActionNames[] = {
    {ActionType::RollDice, "roll_dice"},
    {ActionType::PayRetakeFee, "pay_retake_fee"},
    {ActionType::Buy, "buy"},
    {ActionType::Upgrade, "upgrade"},
    {ActionType::Decline, "decline"},
    {ActionType::PayRent, "pay_rent"},
    {ActionType::AttemptSteal, "attempt_steal"},
    {ActionType::TeleportUnowned, "teleport_unowned"},
    {ActionType::TeleportOwned, "teleport_owned"},
    {ActionType::SellTile, "sell_tile"},
};

const char* actionName(ActionType t) {
  for (const ActionName& n : kActionNames)
    if (n.type == t) return n.name;
  return "unknown";
}

bool parseActionType(const std::string& s, ActionType& out) {
  for (const ActionName& n : kActionNames) {
    if (s == n.name) {
      out = n.type;
      return true;
    }
  }
  return false;
}

const char* effectName(CardEffectType t) {
  switch (t) {
    case CardEffectType::MoneyFlat: return "money_flat";
    case CardEffectType::MoneyPerProperty: return "money_per_property";
    case CardEffectType::PovertySubsidy: return "poverty_subsidy";
    case CardEffectType::GoToJail: return "go_to_jail";
    case CardEffectType::TeleportTile: return "teleport_tile";
    case CardEffectType::SkipTurn: return "skip_turn";
    case CardEffectType::HalveNextDice: return "halve_next_dice";
    case CardEffectType::StealBuff: return "steal_buff";
    case CardEffectType::RentHoliday: return "rent_holiday";
  }
  return "unknown";
}

// ---------------------------------------------------------------------------
// Strict parsing
// ---------------------------------------------------------------------------
json errorJson(const std::string& code, const std::string& message) {
  return json{{"ok", false}, {"error", code}, {"message", message}};
}

// Reads an integer field. Anything that is not a plain JSON integer inside [lo, hi]
// (strings, floats such as 1.5 or 1e3, huge numbers, null, ...) is rejected.
bool readInt(const json& obj, const char* key, bool required, long long lo, long long hi,
             long long def, long long& out, std::string& err) {
  const auto it = obj.find(key);
  if (it == obj.end()) {
    if (required) {
      err = std::string("missing field \"") + key + "\"";
      return false;
    }
    out = def;
    return true;
  }
  if (!it->is_number_integer()) {
    err = std::string("field \"") + key + "\" must be an integer";
    return false;
  }
  if (it->is_number_unsigned() &&
      it->get<unsigned long long>() > static_cast<unsigned long long>(LLONG_MAX)) {
    err = std::string("field \"") + key + "\" is out of range";
    return false;
  }
  const long long v = it->get<long long>();
  if (v < lo || v > hi) {
    err = std::string("field \"") + key + "\" is out of range";
    return false;
  }
  out = v;
  return true;
}

// ---------------------------------------------------------------------------
// Public snapshot (RULES.md section 11)
// ---------------------------------------------------------------------------
json actionJson(const Action& a) {
  return json{{"type", actionName(a.type)},
              {"player_id", a.playerId},
              {"tile_id", a.tileId},
              {"amount", a.amount}};
}

json snapshotJson(const GameState& s) {
  json j;
  j["version"] = 1;
  j["phase"] = phaseName(s.phase);
  j["game_over"] = (s.phase == Phase::GameOver);
  j["winner_id"] = s.winnerId;
  j["tempo"] = s.currentTempo;
  j["max_tempo"] = s.config.maxTempo;
  j["current_player_id"] = s.players.empty() ? -1 : s.players[s.currentPlayerIndex].id;
  j["first_player_id"] = s.firstPlayerIndex;
  j["pending_tile_id"] = s.pendingTileId;
  j["debt"] = {{"amount", s.debt.amount}, {"creditor_id", s.debt.creditorId}};
  j["rent_free_turns_left"] = s.rentFreeTurnsLeft;
  j["last_dice"] = {s.lastDice.first, s.lastDice.second};

  json players = json::array();
  for (const Player& p : s.players) {
    players.push_back({{"id", p.id},
                       {"name", p.name},
                       {"cash", p.cash},
                       {"net_worth", calculateNetWorth(s, p)},
                       {"position", p.position},
                       {"jail_turns", p.turnsInJail},
                       {"consecutive_doubles", p.consecutiveDoubles},
                       {"is_bankrupt", p.isBankrupt},
                       {"skip_turns", p.skipTurns},
                       {"halve_next_dice", p.halveNextDice},
                       {"steal_bonus_percent", p.stealBonusPercent},
                       {"owned_tile_ids", p.ownedTileIds}});
  }
  j["players"] = players;

  json tiles = json::array();
  for (const Tile& t : s.tiles) {
    const bool owned = (t.ownerId >= 0);
    tiles.push_back({{"id", t.id},
                     {"name", t.name},
                     {"type", tileTypeName(t.type)},
                     {"base_price", t.basePrice},
                     {"upgrade_cost", t.upgradeCost},
                     {"base_rent", t.baseRent},
                     {"owner_id", t.ownerId},
                     {"grade", gradeName(t.grade)},
                     {"total_invested", t.totalInvested},
                     {"rent", owned ? rentFor(s, t) : 0},          // what a visitor owes now
                     {"sell_value", owned ? t.totalInvested / 2 : 0}});  // liquidation price
  }
  j["tiles"] = tiles;

  // The deck: only the NUMBER of cards left to draw, never which or in what order.
  json discard = json::array();
  for (int idx : s.deck.discardPile) discard.push_back(s.deck.cards[idx].id);
  j["deck"] = {{"draw_pile_count", s.deck.drawPile.size()}, {"discard_pile", discard}};

  const LegalActions legal = getLegalActions(s);
  json acts = json::array();
  for (const Action& a : legal.actions) acts.push_back(actionJson(a));
  j["legal"] = {{"actions", acts}, {"min_bribe", legal.minBribe}, {"max_bribe", legal.maxBribe}};
  return j;
}

json rulesJson() {
  const GameConfig c;  // the defaults the engine uses
  json cards = json::array();
  for (const Card& card : makeCardDeck().cards) {
    cards.push_back({{"id", card.id},
                     {"title", card.title},
                     {"description", card.description},
                     {"effect", effectName(card.effectType)},
                     {"value", card.value}});
  }
  json r;
  r["ok"] = true;
  r["rules"] = {{"players", kSupportedPlayers},
                {"start_cash", c.startCash},
                {"salary", c.salary},
                {"retake_fee", c.retakeFee},
                {"admin_fee", c.adminFee},
                {"jail_turns", c.jailTurns},
                {"max_doubles_in_row", c.maxDoublesInRow},
                {"max_tempo", c.maxTempo},
                {"tempo_per_semester", c.tempoPerSemester},
                {"exam_season_start", c.examSeasonStart},
                {"exam_bonus_a", c.examBonusA},
                {"exam_bonus_b", c.examBonusB},
                {"rent_factor", {{"D", c.rentFactor[1]}, {"C", c.rentFactor[2]},
                                 {"B", c.rentFactor[3]}, {"A", c.rentFactor[4]}}}};
  r["cards"] = cards;
  return r;
}

json stateResponse(const Engine& e, const std::vector<std::string>& events) {
  json r;
  r["ok"] = true;
  r["events"] = events;
  r["snapshot"] = snapshotJson(e.state);
  return r;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
json cmdNewGame(Engine& e, const json& req) {
  std::string err;
  long long v = 0;
  if (req.contains("players")) {
    if (!readInt(req, "players", false, 0, 1000, kSupportedPlayers, v, err))
      return errorJson("bad_request", err);
    if (v != kSupportedPlayers)
      return errorJson("unsupported_players", "this engine is specified for exactly 4 players");
  }

  unsigned deckSeed = 0;
  if (req.contains("seed")) {
    if (!e.allowSeed) return errorJson("seed_not_allowed", "this engine chooses its own seeds");
    if (!readInt(req, "seed", false, 0, 4294967295LL, 0, v, err))
      return errorJson("bad_request", err);
    e.rng.seed(static_cast<std::mt19937::result_type>(v));
    deckSeed = static_cast<unsigned>(v);
  } else {
    std::random_device rd;
    deckSeed = rd();
  }

  e.state = makeNewGame(kSupportedPlayers, makeBoard(), deckSeed);
  e.state.logEnabled = true;
  e.hasGame = true;
  return stateResponse(
      e, {e.state.players[e.state.firstPlayerIndex].name + " goes first."});
}

json cmdAction(Engine& e, const json& req) {
  const auto typeIt = req.find("type");
  if (typeIt == req.end() || !typeIt->is_string())
    return errorJson("bad_request", "missing string field \"type\"");
  ActionType type = ActionType::RollDice;
  if (!parseActionType(typeIt->get<std::string>(), type))
    return errorJson("unknown_action_type", "unknown action type");

  std::string err;
  long long playerId = 0, amount = 0, tileId = -1;
  if (!readInt(req, "player_id", true, INT_MIN, INT_MAX, 0, playerId, err) ||
      !readInt(req, "amount", false, LLONG_MIN, LLONG_MAX, 0, amount, err) ||
      !readInt(req, "tile_id", false, INT_MIN, INT_MAX, -1, tileId, err))
    return errorJson("bad_request", err);

  Action a;
  a.type = type;
  a.playerId = static_cast<int>(playerId);
  a.amount = amount;
  a.tileId = static_cast<int>(tileId);

  e.state.eventLog.clear();
  if (!applyAction(e.state, a, e.rng)) {
    e.state.eventLog.clear();
    return errorJson("illegal_action", "that action is not allowed right now");
  }
  const std::vector<std::string> events = e.state.eventLog;
  e.state.eventLog.clear();
  return stateResponse(e, events);
}

json handleImpl(Engine& e, const std::string& request) {
  if (request.size() > kMaxRequestBytes)
    return errorJson("request_too_large", "request exceeds 4096 bytes");
  // The JSON library treats a NUL byte as "end of input" and would silently ignore
  // everything after it; a request with hidden trailing bytes is refused instead.
  if (request.find('\0') != std::string::npos)
    return errorJson("bad_request", "request contains a NUL byte");
  const json req = json::parse(request, nullptr, /*allow_exceptions=*/false);
  if (req.is_discarded() || !req.is_object())
    return errorJson("bad_request", "request must be a JSON object");
  const auto cmdIt = req.find("cmd");
  if (cmdIt == req.end() || !cmdIt->is_string())
    return errorJson("bad_request", "missing string field \"cmd\"");
  const std::string cmd = cmdIt->get<std::string>();

  if (cmd == "get_rules") return rulesJson();
  if (cmd == "new_game") return cmdNewGame(e, req);
  if (!e.hasGame) return errorJson("no_game", "start a game with new_game first");
  if (cmd == "get_state") return stateResponse(e, {});
  if (cmd == "action") return cmdAction(e, req);
  return errorJson("unknown_cmd", "unknown command");
}

std::string dump(const json& j) {
  return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

}  // namespace

Engine::Engine() {
  std::random_device rd;
  std::seed_seq seq{rd(), rd(), rd(), rd()};
  rng.seed(seq);
}

std::string handleRequest(Engine& engine, const std::string& request) {
  try {
    return dump(handleImpl(engine, request));
  } catch (const std::exception& ex) {
    return dump(errorJson("internal_error", ex.what()));
  } catch (...) {
    return dump(errorJson("internal_error", "unexpected failure"));
  }
}

std::string snapshotToJson(const GameState& state) { return dump(snapshotJson(state)); }
