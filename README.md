# FirstGame (HUST_MONOPOLY)

A C++17, headless board-game simulation inspired by Monopoly and adapted to a HUST campus theme.

## Current repository status

- Core game logic lives in `/home/runner/work/FirstGame/FirstGame/HUST_MONOPOLY/GameLogic.cpp`.
- The playable simulation entry point is `/home/runner/work/FirstGame/FirstGame/HUST_MONOPOLY/main.cpp`.
- Rule-based tests are in `/home/runner/work/FirstGame/FirstGame/HUST_MONOPOLY/tests.cpp`.
- There are currently no build scripts (no Makefile/CMake); build and run are done directly with `g++`.

## Build and run

From `/home/runner/work/FirstGame/FirstGame/HUST_MONOPOLY`:

```bash
# Build and run scripted tests
g++ -std=c++17 -Wall -Wextra -O2 tests.cpp GameLogic.cpp -o tests
./tests

# Build and run bot simulation
g++ -std=c++17 -Wall -Wextra -O2 main.cpp GameLogic.cpp -o sim
./sim
```

Example simulation flags:

```bash
./sim --games 5000 --seed 7
./sim --verbose --seed 3
./sim --salary 20000 --cash 400000 --rentx 3
```

## Developer docs

See `/home/runner/work/FirstGame/FirstGame/DEVELOPER.md` for a code-oriented overview mapped to current files and behaviors.
