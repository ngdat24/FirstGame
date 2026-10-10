# HUST Monopoly — JSON Protocol v1

The engine is one function: `std::string handleRequest(Engine&, const std::string& json)`.
Text in, text out; it never throws and an invalid or illegal request never changes the game.
It does not care how the text travels (a direct call from a GDExtension, HTTP on localhost,
a socket, ...). Rules are in `RULES.md`; this file is only the wire format.

## Requests
| Request | Notes |
|---|---|
| `{"cmd":"get_rules"}` | constants and the public card texts; no game needed |
| `{"cmd":"new_game"}` | exactly 4 players (`"players":4` is accepted, anything else is refused) |
| `{"cmd":"new_game","seed":N}` | dev/tests only: refused unless `Engine::allowSeed`; N is 0..4294967295 |
| `{"cmd":"get_state"}` | the current snapshot |
| `{"cmd":"action","type":T,"player_id":P}` | optional `"tile_id"` (default -1) and `"amount"` (default 0) |

Action types `T`: `roll_dice`, `pay_retake_fee`, `buy`, `upgrade`, `decline`, `pay_rent`,
`attempt_steal` (`amount` = bribe), `teleport_unowned` / `teleport_owned` / `sell_tile`
(`tile_id` = target). Which ones are allowed right now is listed in `snapshot.legal`; a client
should only ever send those.

Parsing is strict: the request must be a JSON object of at most 4096 bytes with no NUL byte;
numeric fields must be plain integers (no strings, floats such as `1.5` or `1e3`, null, or
numbers outside the range).

## Responses
- Success: `{"ok":true,"events":[...],"snapshot":{...}}` (`new_game`, `get_state`, `action`).
  `events` are text lines describing what the request caused (empty for `get_state`).
  `get_rules` returns `{"ok":true,"rules":{...},"cards":[...]}`.
- Failure: `{"ok":false,"error":CODE,"message":TEXT}` with CODE one of `bad_request`,
  `request_too_large`, `no_game`, `unknown_cmd`, `unknown_action_type`, `illegal_action`,
  `unsupported_players`, `seed_not_allowed`, `internal_error`.

## Snapshot (public state only, RULES.md section 11)
```
version, phase, game_over, winner_id, tempo, max_tempo, current_player_id, first_player_id,
pending_tile_id, rent_free_turns_left, last_dice:[d1,d2],
debt:{amount, creditor_id},                    // creditor -1 = the bank
players:[{id, name, cash, net_worth, position, jail_turns, consecutive_doubles, is_bankrupt,
          skip_turns, halve_next_dice, steal_bonus_percent, owned_tile_ids}],
tiles:[{id, name, type, base_price, upgrade_cost, base_rent, owner_id, grade, total_invested,
        rent, sell_value}],                    // rent = what a visitor owes now (0 if unowned)
deck:{draw_pile_count, discard_pile:[card ids]},
legal:{actions:[{type, player_id, tile_id, amount}], min_bribe, max_bribe}
```
- `phase`: `await_roll`, `await_landing_choice`, `await_corner_choice`, `await_liquidation`,
  `game_over`.
- Tile `type`: `institute`, `start`, `special_corner`, `retake_exam`, `academic_warning`,
  `card`. `grade`: `none`, `D`, `C`, `B`, `A`.
- `attempt_steal` is listed once with `amount` 0: send any bribe in `[min_bribe, max_bribe]`.
- The snapshot never contains the draw pile (neither order nor contents) or any RNG state.

## Known limits (planned, not frozen)
- `events` are plain text. A UI that animates dice, moves and card draws will want structured
  events (type + fields); that is the next addition, designed together with the Godot board.
- No authentication or per-seat identity: any caller may send actions for the current player.
  That belongs to the server layer later.
