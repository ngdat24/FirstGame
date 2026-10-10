# HUST Monopoly — Frozen Rules Spec v1.0

Describes exactly what the engine (`GameLogic.cpp`) does. If the code and this file ever
disagree, that is a bug in one of them: fix it and add a test.
Money is in VND. All numbers are defaults of `GameConfig` unless stated otherwise.

**Freeze policy.** Bug (code ≠ spec): fix now, with a regression test. Ambiguity: add it to
section 10, decide, update the spec. Balance change: only after playtest data, bump the
version. New mechanic: backlog, not during Phase 2.

---

## 1. Setup
- 4 players, ids 0–3, each starts on tile 0 with 700,000 cash.
- The **first player is random** (`makeNewGame`, from its seed); turn order then follows the
  seat ids cyclically.
- 40 tiles: `0` Start · `10` Retake Exam Room · `20` Academic Affairs Office (Special
  Corner) · `30` Academic Warning · `5, 15, 25, 35` card tiles (`TileType::Other`) · the other
  32 are Institutes.
- Institute number *k* (0..31, in board order): price `100,000 + 10,000·k`, upgrade cost
  `price/2` per grade step, `baseRent = price/10`.

## 2. Turns and tempos
- A **tempo** = every living player has had their turn. Tempo 1 starts with the first player;
  tempo *n+1* begins when the turn comes back to the first player's seat (even if that player
  was expelled or is skipped). Skipped turns still count toward the tempo.
- A turn: **roll → move → resolve the tile (maybe one choice) → next player.**
- **Doubles** (not in jail): after the landing is resolved the same player rolls again. A
  **third double in the same turn** sends the player straight to the Retake Exam room without
  moving; the turn ends.
- The turn ends immediately (no bonus roll even on doubles) when the player is sent to jail
  or draws *Đi chơi với bạn gái quên học*.
- **Start salary** (100,000) is paid when a normal move passes or lands on tile 0 (dice rolls
  and card moves). Teleporting at the Special Corner never pays it.

## 3. Retake Exam room (jail)
- Sent there by: Academic Warning tile, 3 doubles in a turn, card *Quên điểm danh giải tích*.
  Effect: position = 10, 3 jail turns, turn ends. Landing on tile 10 by moving is just visiting.
- On each of their turns, a jailed player may **pay 50,000 and roll normally**, or **roll**:
  - doubles → free; they move by the roll, **no extra roll**;
  - not doubles → one jail turn used up; on the **3rd failed roll** they must pay 50,000 (a
    forced debt, see 6) and then move by that roll.
- A jailed player still collects rent and keeps their tiles.

## 4. Tiles
- **Institute, unowned:** may **buy** at base price (becomes grade D) or **decline**. No prompt
  if they cannot afford it.
- **Institute, own:** may **upgrade** one grade (D→C→B→A) for its upgrade cost, or decline.
  No prompt at grade A or if unaffordable. Passing Start gives no upgrade right.
- **Institute, someone else's:** must choose **pay rent** or **attempt a steal**. There is no
  way to ignore it.
- **Start / Retake Exam room:** nothing (besides salary on Start).
- **Academic Warning:** go to jail (section 3).
- **Special Corner:** may **teleport** (fee 20,000) to *any unowned Institute* (and buy it at
  grade D) or *any Institute they own below grade A* (and upgrade it), or **decline**. Only
  targets they can afford (fee + price/upgrade cost) are offered. If there are none the tile
  does nothing. The fee is only charged on teleporting.
- **Card tiles:** draw the top card of the single shared deck; its effect applies at once.

## 5. Rent and stealing
- **Rent** = `baseRent × factor`, factor = D 2 · C 4 · B 8 · A 16. Paid to the owner.
- **Steal (Proxy Test-Taker):** the player picks a bribe from **1 to their cash** (cash only,
  no loans). Success chance = `min(1, bribe / totalInvested + bonus)`, where `totalInvested`
  = base price + all upgrade costs paid, and `bonus` is the one-shot Phao thi card bonus (used
  up by the attempt either way). The bribe is always lost (paid to the bank).
  - **Success:** the tile changes owner and **keeps its grade and totalInvested**.
  - **Failure:** the bribe is lost and **no rent is owed** (single penalty).

## 6. Debts, liquidation, expulsion
- Forced payments are: rent, the forced jail fee, and card fines. Voluntary payments (buy,
  upgrade, teleport, bribe, voluntary jail fee) are only legal with cash in hand.
- A forced payment you can afford is paid immediately. If you can't, you must **sell Institutes
  to the bank one at a time (your choice)** at **50% of totalInvested** (rounded down). A sold
  tile becomes unowned (grade None, invested 0). As soon as cash covers the debt it is paid and
  the turn continues.
- If all tiles are sold and the debt is still not covered, the player is **expelled**: their
  remaining cash goes to the creditor (vanishes if the creditor is the bank), they own nothing
  and are skipped forever.
- If only one player remains, the game ends immediately and they win.

## 7. Exam Season
At the start of every tempo whose number within its 16-tempo semester is **13–16**, every living
player receives **50,000 per grade-A Institute and 20,000 per grade-B Institute** from the
bank. Tempo 1 has no payout.

## 8. Cards (11 cards, one shared deck)
The deck is shuffled at game start; played cards go to a discard pile, which is reshuffled
when the draw pile runs out. Durations in the last column.

| # | Card | Effect | Duration |
|---|------|--------|----------|
| 1 | Thanh tra đồ án đột xuất | Drawer pays 30,000 × Σ weights of their Institutes (D1 C2 B4 A8) | instant (forced debt) |
| 2 | Quỹ khuyến học cựu sinh viên | Player with the **lowest net worth** gets 300,000 (ties broken randomly; may be the drawer or not) | instant |
| 3 | Cúp điện toàn trường | **No rent** for one round | until the drawer's turn comes again (counted in turns, skipped turns included); steals still allowed |
| 4 | Phao thi chất lượng cao | +25 percentage points on the drawer's next steal | until used (win or lose); doesn't stack |
| 5 | Quên điểm danh giải tích | Jail | see 3 |
| 6 | Đi chơi với bạn gái quên học | Drawer's next turn is skipped | 1 turn; stacks if drawn again |
| 7 | Đi học bù ca 4 kẹt xe | Drawer's next movement is halved, rounded down (11→5, 12→6, min 1) | until the drawer actually moves (failed jail rolls and the triple-doubles jail don't use it) |
| 8 | Đóng tiền học phí kỳ phụ | Pay 150,000 | instant (forced debt) |
| 9 | Ăn phải quán cổng phụ đau bụng | Pay 100,000 | instant (forced debt) |
| 10 | Mất vé xe ở thư viện Tạ Quang Bửu | Pay 50,000 | instant (forced debt) |
| 11 | Ăn cơm tiệm gần cổng Parabol trúng thưởng | Gain 100,000 | instant |

(The engine also supports a "teleport to tile N" effect; no card uses it.)

## 9. End of game and winner
- The game ends when **tempo 64 concludes** (after the last player's turn) or when one player
  is left.
- **Net worth** = cash + Σ `totalInvested` of owned Institutes. Highest wins; ties → more
  cash → lower player id.

## 10. Decisions and simplifications (all approved)
1. Buying and upgrading are optional (the player can decline).
2. Special Corner can be declined; the fee is charged only when teleporting.
3. Steal chance uses `totalInvested`, not the original price.
4. An expelled player's leftover cash goes to the creditor.
5. The game ends early when one player remains.
6. Landing on the jail tile by moving is only a visit.
7. Net-worth tie-break by cash then id (practically unreachable).
8. Opponents' cash and status effects are public (see 11).

## 11. Public vs hidden state
**Never sent to clients:** `deck.drawPile` (neither order nor contents: it would reveal the
next card), `deckRng`, and any server dice generator or seed. A client may be told only how many
cards remain in the draw pile.
**Public:** everything else — tiles (owner, grade, totalInvested), every player's cash,
position, jail turns, status effects (`skipTurns`, `halveNextDice`, `stealBonusPercent`),
`phase`, `currentPlayerIndex`, tempo, `pendingTileId`, the pending debt, `rentFreeTurnsLeft`,
the last dice, the **discard pile** (cards already played, face up) and the event log.
Note: because discards are public, a careful player can work out which cards are still in the
draw pile. Accepted for now.
All randomness (dice, shuffles, tie-breaks) stays in the engine/server; a client only sends
actions.

## 12. Action API (what a UI needs)
```
bool          applyAction(GameState&, const Action&, std::mt19937& rng);
LegalActions  getLegalActions(const GameState&);
```
- `Action { type, playerId, amount, tileId }`. Only `players[currentPlayerIndex]` may act.
- `applyAction` returns **false and changes nothing** for anything illegal (guaranteed by the
  fuzz test). The UI should never need to try-and-see: call `getLegalActions` after every state
  change and enable exactly those buttons. The server calls `applyAction` anyway.
- `getLegalActions` returns `actions` (every discrete legal action) plus `minBribe`/`maxBribe`.
  `AttemptSteal` appears once with `amount = 0`; the UI supplies a bribe in
  `[minBribe, maxBribe]`.

| Phase | Legal actions | Fields used |
|-------|---------------|-------------|
| `AwaitRoll` | `RollDice`; `PayRetakeFee` (jailed and cash ≥ 50,000) | – |
| `AwaitLandingChoice` (`pendingTileId`) | unowned: `Buy`, `Decline` · own: `Upgrade`, `Decline` · opponent's: `PayRent`, `AttemptSteal` | `AttemptSteal`: `amount` |
| `AwaitCornerChoice` | `Decline`; `TeleportUnowned`; `TeleportOwned` | `tileId` |
| `AwaitLiquidation` | `SellTile` for each owned Institute | `tileId` |
| `GameOver` | none (`winnerId` is set) | – |

Helpers a UI can call: `rentFor(state, tile)`, `calculateNetWorth(state, player)`,
`countAlivePlayers(state)`. Steal chance to display: `min(1, bribe/totalInvested + bonus%)`.
`applyRoll(state, d1, d2)` injects dice: server/test only, never exposed to clients.

The JSON wire format that carries all of this is in `PROTOCOL.md` (v1); a UI should talk to the
engine only through it.

## 13. How this spec is verified
`tests.cpp` (1341 checks: every rule above), `tests_protocol.cpp` (the JSON layer: strict
parsing, hidden state, full games played by a client that sees only JSON, endpoint fuzzing),
`./sim --games N` (bot games with a full
consistency check after every action), `./sim --fuzz` (garbage actions must be rejected without
changing the state, and `getLegalActions` must agree exactly with `applyAction`), and all of it
under `-fsanitize=address,undefined`.
