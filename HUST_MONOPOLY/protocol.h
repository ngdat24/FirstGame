#pragma once

#include <random>
#include <string>

#include "GameLogic.h"

// The whole engine as seen from OUTSIDE (Godot, a CLI, later a network server):
// text in, text out. Nothing else crosses this boundary, so the transport (a direct
// function call from a GDExtension, HTTP on localhost, a socket, ...) can change without
// touching the rules or the UI.
//
// Requests (JSON objects):
//   {"cmd":"new_game"}                            4 players, random seed
//   {"cmd":"new_game","seed":123}                 only if Engine::allowSeed (dev/tests)
//   {"cmd":"get_state"}
//   {"cmd":"get_rules"}                           constants + the public card texts
//   {"cmd":"action","type":"buy","player_id":0}   optional: "amount", "tile_id"
//
// Action types: roll_dice, pay_retake_fee, buy, upgrade, decline, pay_rent, attempt_steal,
// teleport_unowned, teleport_owned, sell_tile.
//
// Every response is {"ok":true, ...} or {"ok":false,"error":"<code>","message":"..."}.
// Success responses to new_game/get_state/action carry "snapshot" (the public state and the
// legal actions) and "events" (text lines describing what just happened).
// An invalid or illegal request NEVER changes the game.
//
// See RULES.md section 11: the snapshot never contains the deck order or any RNG state.

struct Engine {
  GameState state;
  std::mt19937 rng;        // dice. Server-side only; never exposed.
  bool hasGame = false;
  bool allowSeed = false;  // dev/tests only: a client choosing the seed could predict the game
  Engine();                // seeds `rng` from std::random_device
};

// Never throws. Always returns a JSON text.
std::string handleRequest(Engine& engine, const std::string& request);

// The public state only, as JSON text (also used by the tests).
std::string snapshotToJson(const GameState& state);
