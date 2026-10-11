# HUST Monopoly

A 4-player Monopoly-style board game set on the Hanoi University of Science and Technology
(HUST) campus, written in C++17. Institutes are graded D → A instead of houses and hotels, you can
hire a "Proxy Test-Taker" to steal a rival's Institute, Exam Season pays scholarships, the jail is
the Retake Exam room, and an 11-card deck brings student life into the game.

**Status:** rules engine finished and frozen (v1.0), JSON protocol v1, text hotseat client.
Next: a Godot board UI that talks to the engine through the protocol.

## Documents (the source of truth)
- [`HUST_MONOPOLY/RULES.md`](HUST_MONOPOLY/RULES.md) — the frozen rules spec: every rule, all 11
  cards, public vs hidden state, and the action API.
- [`HUST_MONOPOLY/PROTOCOL.md`](HUST_MONOPOLY/PROTOCOL.md) — the JSON wire format a UI talks to.
- `basic_rules.txt`, `update1.txt`, `first_thought_of_claude.txt` — early design notes. Kept for
  history; `RULES.md` supersedes them.

## Build and run
Needs `g++` with C++17. Run everything from the `HUST_MONOPOLY` folder.
```
cd HUST_MONOPOLY

# Play (4 humans share the keyboard; --auto lets random players play a whole game)
g++ -std=c++17 -O2 play.cpp protocol.cpp GameLogic.cpp -o play
./play

# Bot simulator (balance numbers, consistency checks, fuzzing)
g++ -std=c++17 -O2 main.cpp GameLogic.cpp -o sim
./sim --games 2000          # also: --fuzz  --nocards  --verbose --seed 7

# Tests
g++ -std=c++17 -O2 tests.cpp GameLogic.cpp -o tests && ./tests
g++ -std=c++17 -O2 tests_protocol.cpp protocol.cpp GameLogic.cpp -o tests_protocol && ./tests_protocol
```
`tests_protocol` takes about 20 seconds (`--quick` for a short run). On a Windows console run
`chcp 65001` first so the Vietnamese card texts display correctly. Compiled programs are
ignored by git (see `.gitignore`).

## Code map (`HUST_MONOPOLY/`)
| File | What it holds |
|---|---|
| `tile.h`, `player.h`, `card.h`, `GameState.h` | plain data: tiles, players, cards, the game state and `GameConfig` (all tunable numbers) |
| `board_data.h`, `card_data.h` | the 40-tile board and the 11-card deck, as data |
| `GameLogic.h/.cpp` | the rules engine: `applyAction`, `getLegalActions`, turn flow |
| `protocol.h/.cpp` | the engine as JSON in / JSON out |
| `play.cpp` | text hotseat client (uses only the JSON protocol) |
| `main.cpp` | bot simulator and fuzzer |
| `tests.cpp`, `tests_protocol.cpp` | tests for the rules and for the JSON layer |
| `third_party/json.hpp` | [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 (MIT) |

## Changing the rules
1. Update `RULES.md` first (and `PROTOCOL.md` if the wire format changes).
2. Change the code. Numbers live in `GameConfig`, `board_data.h` and `card_data.h`.
3. Run the tests, then `./sim --fuzz` and look at the balance numbers.
4. Merge only when everything passes.
