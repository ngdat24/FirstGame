# Developer Notes (Code-Aligned)

This document reflects the current implementation in this repository.

## 1) Source layout

- `HUST_MONOPOLY/GameLogic.cpp`, `HUST_MONOPOLY/GameLogic.h`: game state transitions, actions, and rule enforcement.
- `HUST_MONOPOLY/main.cpp`: headless multi-game bot simulation (`sim` binary).
- `HUST_MONOPOLY/tests.cpp`: deterministic rule tests (`tests` binary).
- `HUST_MONOPOLY/board_data.h`: 40-tile placeholder board builder (`makeBoard()`).

## 2) Build commands (current)

Run from `/home/runner/work/FirstGame/FirstGame/HUST_MONOPOLY`:

```bash
g++ -std=c++17 -Wall -Wextra -O2 tests.cpp GameLogic.cpp -o tests
g++ -std=c++17 -Wall -Wextra -O2 main.cpp GameLogic.cpp -o sim
```

## 3) Rule behaviors currently implemented

- Failed steal has a **single penalty**: bribe is lost, no rent charged (`attemptSteal` in `GameLogic.cpp`).
- Passing Start gives salary only (`movePlayer` in `GameLogic.cpp`).
- Debt handling uses forced liquidation with 50% sell-back of invested value (`handleLiquidation` in `GameLogic.cpp`).
- Game ends at max tempo and winner is selected by net worth with cash tiebreak (`endGame` in `GameLogic.cpp`).

## 4) Vietnamese summary / Tóm tắt tiếng Việt

- Cấu trúc mã chính nằm trong thư mục `HUST_MONOPOLY`, với logic ở `GameLogic.cpp`, mô phỏng ở `main.cpp`, và kiểm thử ở `tests.cpp`.
- Hiện chưa có script build (không có Makefile/CMake); biên dịch trực tiếp bằng `g++`.
- Luật hiện tại trong code: trượt cướp chỉ mất tiền hối lộ, qua Start chỉ nhận lương, thiếu tiền thì bán tài sản 50%, và kết thúc game theo tempo tối đa.
