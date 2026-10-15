# PRISM Guideline Speed and Real-Time Agent Implementation Plan (Milestone 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** PRISM speeds up like modern guideline Tetris, and an agent (local policy or Jev) plays its board under
real gravity with human-like timed inputs, while the driver keeps playing through the real-time states that used
to end a run.

**Architecture:** `tetris_rules.lua` gets the guideline gravity curve and 20G. The agent adapter stops freezing
gravity: it versions decisions by a state key without the piece's row, re-plans the chosen landing from the
piece's current position before every input, feeds one input every 0.07 s through the shared controller and
reports `busy` until the drop. The driver turns on real-time handling when `game_describe` advertises
`state_tracking: "game_epoch"`: it waits out `busy`, empty action lists and `stale_state`/`busy` act rejections
instead of exiting, and counts re-decides.

**Tech Stack:** LuaJIT game scripts (`tetris_rules.lua`, `tetris_agent.lua`, `prism.lua`), C++20 driver
(`Phasma/GameDriver`, `Protocol.cpp`, `main.cpp`), luajit and CTest tests, PhasmaPlayer MCP for the live run.

**Spec:** `docs/superpowers/specs/2026-10-15-prism-showcase-design.md` (sections Speed, Real-time agent)

## Global Constraints

- Gravity interval below level 20: `(0.8 - (L - 1) * 0.007) ^ (L - 1)` seconds per row; level 20 and above:
  20G (the piece drops to its landing row at once); lock delay 0.5 s with 15 resets unchanged; soft drop keeps
  `min(interval, 0.035)`; level = floor(lines / 10) + 1.
- Agent inputs run one every 0.07 s, then a hard drop; `busy` is true while a placement is running.
- The state key excludes the active row and timers; it covers board, active kind/rotation/column, hold,
  `can_hold`, next queue, lines, level, score and mode.
- If a piece locks before the agent acts, the agent decides again on the next piece (paid; user-approved).
- Driver: no new command-line flag; real-time handling follows `state_tracking == "game_epoch"`. Turn-based
  games keep today's strict behaviour. Invalid or illegal actions, protocol violations and Jev errors still end
  the run. No automatic retry of a Jev call that failed.
- Layout rules from CLAUDE.md (one statement per line, wrap by operand, no code line over ~120 columns, LF);
  `clang-format -i` on touched C++; no commits.

## Review Focus

- Pause during a running placement: inputs stop, gravity stops, the driver waits without a deadline and resumes
  after unpause. Pinned by Task 2's pause test and Task 3's wait classification.
- A human key press during an agent placement: the agent's inputs stop at once and the human owns the piece.
  Pinned by Task 2's takeover test.
- A placement that becomes unreachable while the piece falls: the adapter stops feeding inputs, the piece keeps
  falling, the driver decides again after the lock. Pinned by Task 2's unreachable test.
- Hold placements: the route starts with `hold`; after it the target is the same landing as a play placement.
  Pinned by Task 2's hold route test.
- 20G: no infinite gravity loop and the piece lands at once. Pinned by Task 1's 20G test.

---

### Task 1: Guideline gravity and 20G

**Files:** Modify `Games/PrismTetris/Assets/Scripts/tetris_rules.lua` (`gravity_interval`, `State:tick`);
Test `Games/PrismTetris/tests/rules_test.lua`.

- [ ] **Step 1: Failing tests** (behaviour through `tick`): level 2 does not fall after 0.79 s and falls one row
  by 0.80 s; level 10 falls one row by 0.065 s but not after 0.063 s; level 20 puts the piece on `ghost_y()` after a
  0.001 s tick and still locks only after the 0.5 s delay.
- [ ] **Step 2: Run** `build-ninja-physics/Release/luajit.exe Games/PrismTetris/tests/rules_test.lua`; expected
  FAIL (level 2 currently uses 0.8 s, level 10 uses 0.134 s, 20G absent).
- [ ] **Step 3: Implement** the guideline interval; when the interval is 0 (level 20+) drop the piece to
  `ghost_y()` before the tick loop so the grounded branch consumes the frame.
- [ ] **Step 4: Run; expected PASS** (all rules checks).

### Task 2: Real-time agent adapter

**Files:** Modify `Games/PrismTetris/Assets/Scripts/tetris_agent.lua`, `Games/PrismTetris/Assets/Scripts/prism.lua`;
Test `Games/PrismTetris/tests/agent_test.lua`.

**Interfaces:** Produces `agent.tick(dt)` (called every frame by `prism.lua`), `state_key` in the driver spec,
`busy` in observations.

- [ ] **Step 1: Failing tests:** after `reset`, ticking 1.2 s of frames moves the piece down a row while the
  `state_id` stays the same; `act` returns `busy = true` and a placement completes after ticking until not busy,
  with inputs spaced by 0.07 s (count frames); a human key during `busy` stops the inputs (piece no longer moves
  by itself sideways) and clears agent mode; pausing during `busy` stops inputs and gravity, unpausing resumes; a
  hold placement completes; a placement made unreachable mid-route stops feeding inputs. Existing assertions keep
  working with a `settle()` helper that ticks until not busy.
- [ ] **Step 2: Run** agent, entry and render tests; expected FAIL (gravity frozen, act instant).
- [ ] **Step 3: Implement:**
  - `tetris_agent.lua`: cache key without the active row; `placements` also returns id -> landing key;
    `state_key` for the driver spec from the public observation without `active.y`; `act` stores the target
    landing key and marks agent mode (no instant execution); `tick(dt)` every 0.07 s re-plans the route from the
    current position to the target and applies its first command (a leading `hold` switches the target from
    `hold:` to `play:`; a `drop` ends the placement; no route or a refused command ends it); nothing runs while
    not playing, while game over or when agent mode is off (target cleared); `reset` clears the target; observe
    reports `busy = target ~= nil`.
  - `prism.lua`: forward-declare `agent`; in `control`, a human key clears agent mode; agent mode calls
    `agent.tick(dt)` instead of reading human input; gravity ticks in both modes (soft drop only for humans).
- [ ] **Step 4: Run** all four Lua suites; expected PASS.

### Task 3: Driver real-time handling

**Files:** Modify `Phasma/GameDriver/Protocol.h`, `Protocol.cpp`, `main.cpp`; Test `tests/ProtocolTests.cpp`;
Docs `Phasma/GameDriver/README.md`, `docs/wiki/architecture/mini-game-authoring.md`.

**Interfaces:** Produces `bool IsRealtime(const Json &description)` and
`ActOutcome ClassifyActResult(const Json &result, bool realtime)` with
`enum class ActOutcome { Observation, Redecide, Reject }`.

- [ ] **Step 1: Failing tests** in `ProtocolTests.cpp`: `IsRealtime` true only for
  `state_tracking == "game_epoch"`; `ClassifyActResult` returns `Observation` for a protocol observation,
  `Redecide` for `{ok:false, error:{code:"stale_state"|"busy"}}` when real-time, `Reject` for those codes when not
  real-time and for `illegal_action`/`invalid_request`/missing codes.
- [ ] **Step 2: Build and run** `ctest -R game-driver-protocol`; expected FAIL (functions missing).
- [ ] **Step 3: Implement** both functions; in `main.cpp` pass `realtime` from the description into `WaitReady`
  (real-time: wait while busy or while no actions and not terminal, no 30 s deadline, Ctrl+C still cancels) and
  `PlayStep` (on `Redecide` trace an `{"event":"redecide"}` row, re-observe and continue; on `Reject` end with the
  game's error code); count re-decides in the totals, finish row and `Finished:` line.
- [ ] **Step 4: Build warning-free; run protocol and launcher tests; expected PASS.**

### Task 4: Live verification

- [ ] **Step 1:** `run-tetris.ps1 -Policy local -Steps 60 -Seed 42` on display 1: pieces visibly fall and move
  with timed inputs, the run finishes, the summary is written; log clean.
- [ ] **Step 2:** `run-tetris.ps1 -Policy jev -UseVault -Steps 500 -Seed 42 -ShowWindow`: Jev plays to game over
  or the cap under real gravity; record placements, re-decides, cost; quarantine count unchanged; log clean.
- [ ] **Step 3:** Show the user a screenshot mid-run and the numbers.
