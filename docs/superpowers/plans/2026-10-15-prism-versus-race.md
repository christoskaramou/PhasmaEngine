# PRISM Versus Race Implementation Plan (Milestone 3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** From PRISM's menu you start a side-by-side race against Jev on identical pieces: your board on the
left, Jev's on the right, each with its own level and speed; the first board to top out loses and a results
card shows both runs and Jev's calls and cost.

**Architecture:** PRISM runs as a private Player with MCP on (new `run.ps1`), with `PhasmaGameDriver.exe` in
the runtime's `Assets/Bots`. Two rules games (`game` = you, `rival` = Jev) start from one seed. Both 3D boards
are built at load; your board always sits at x = -7.5 and Jev's at +7.5. Solo centres the camera and aurora
on your board and hides Jev's; versus centres on x = 0. `prism_jev.lua` starts the driver through `proc`
(attach, no reset), parses its output for think time, calls and tokens, and stops it. The agent adapter plays
whichever game the agent seat holds: your game in solo (with takeover), Jev's game in a race (your keys never
reach it). The race end is terminal for the driver, so it exits with its `Finished:` line.

**Tech Stack:** LuaJIT game scripts, Python PIL art (`tools/make_art.py`), PowerShell launcher, C++20 driver
(`Phasma/GameDriver/main.cpp`), luajit and CTest tests, PhasmaPlayer MCP for the live race.

**Spec:** `docs/superpowers/specs/2026-10-15-prism-showcase-design.md` (Versus race, HUD, Launch, Geometry)

## Global Constraints

- Boards at x = -7.5 (you) and +7.5 (Jev); cell pitch 1.0, cube 0.9; you appear on the left.
- One seed for both games, each with its own level and speed; the first to top out loses; both topping out in
  the same frame is a draw.
- Driver arguments: `--url http://127.0.0.1:<mcp_port>/mcp --policy jev --key-source vault --steps 10000`.
  No `--seed` (it would reset the game and end the race); no `--realtime` (removed in Milestone 2). The port
  comes from the runtime's `Agent/agent_config.json` (`mcp_port`, default 8765).
- In a race your keys drive only your board and never end agent mode; Jev drives only its board.
- Race over is terminal to the driver; it exits with `Finished:`; `proc.stop()` after 3 s is the fallback.
- Pool everything at load (both boards, both overlays); switching modes creates no nodes or materials.
- Only your seat plays sounds and drives shared stage effects (camera shake, bloom flare, aurora shift).
- A race never changes the solo best score.
- Missing or unreadable key: the driver's failure line ends the race with that message (the key panel is
  Milestone 4).
- HUD (user decision 2026-10-15): a score/level/lines card above each board, plus hold and next-five previews
  on the outer side of each board; Jev's think time, calls and cost at the bottom.
- CLAUDE.md layout rules, LF line endings, `clang-format -i` on touched C++, no commits, test windows on
  display 1, live tests on their own MCP port.

## Review Focus

- Your key press mid-race: your board moves and Jev keeps playing (agent mode stays on). Pinned in Task 6.
- You top out first: the race ends, the adapter reports terminal, the driver exits instead of waiting forever.
  Pinned in Task 6.
- The driver never starts (exe missing or quarantined) or dies mid-race (key, network, Jev error): the race
  ends with its message. Pinned in Task 6.
- Pause mid-race: both boards and Jev's running placement stop; resume continues both. Pinned in Task 6.
- Switching solo and versus repeatedly: no node or material creation, overlay and camera re-fit, Jev's board
  fully hidden in solo. Pinned in Tasks 3-6.

---

### Task 1: Private Player launcher

**Files:** Rewrite `Games/PrismTetris/run.ps1`; README launch section.

- [ ] **Step 1:** `run.ps1 [-Port 8876]`: copy `PhasmaPlayer.exe`, its DLLs, `RuntimeAssets` and `MaterialX`
  from `build-ninja-physics/Release` into `build-prism-tetris/player`; copy `Assets/{Audio,Scenes,Scripts,
  Textures}` into the runtime `Assets` (keeping its `Save/`); write runtime `Assets/Agent/agent_config.json`
  (`mcp = true`, `mcp_port`) and `phasma_settings.json` (project path, startup scene, Vulkan); copy
  `build-game-driver/Release/PhasmaGameDriver.exe` into runtime `Assets/Bots` (warn when missing: races
  unavailable); start the Player with `--api vulkan --display 1`, window shown; reuse a running instance on
  the same port (instance file), refuse an occupied port. Nothing is written into the repo's `Assets`.
- [ ] **Step 2: Verify live** on a free port: MCP `execute_lua` returns `prism ~= nil`, the log has no
  warnings or errors, the Player closes.

### Task 2: Driver step lines carry tokens

**Files:** `Phasma/GameDriver/main.cpp`; Test `tests/RealtimeLoopTests.ps1`.

**Interfaces:** Produces step lines `<model> step <n> action=<id> score=<s> decision_ms=<ms> input_tokens=<t>`
and re-decide lines `<model> re-decide <n> after "<code>" input_tokens=<t>`.

- [ ] **Step 1: Failing tests:** the accepted-run scenario prints `input_tokens=0` on every step line; the
  stale scenario prints `input_tokens=0` on every re-decide line.
- [ ] **Step 2: Run** `ctest -R game-driver-realtime`; expected FAIL.
- [ ] **Step 3: Implement**; **Step 4:** build warning-free, ctest PASS.

### Task 3: Board visibility and lead flag

**Files:** `Assets/Scripts/prism_board3d.lua`; Test `tests/render_test.lua`.

**Interfaces:** `Board.new{ rules, stage, origin, lead }` (lead defaults true); `Board:set_shown(shown)`;
`board.shown`.

- [ ] **Step 1: Failing tests:** a second board at x = +7.5 with `lead = false`; `set_shown(false)` hides
  every node it owns (cells, piece, ghost, well bars, sweeps) and zeroes its glows; `sync` while hidden shows
  nothing; `set_shown(true)` restores the well and repaints cells; the non-lead board's hard drop, line clear
  and level-up do not shake the camera, flare bloom or shift the aurora; no creation after load.
- [ ] **Step 2: Run** render_test; expected FAIL. **Step 3: Implement** (store well bars in `self.bars`).
  **Step 4:** all Lua suites PASS.

### Task 4: Stage framing for solo and versus

**Files:** `Assets/Scripts/prism_stage.lua`; Test `tests/render_test.lua`.

**Interfaces:** `Stage:set_layout(layout)` with `{ center_x, top, height }` in design px (solo `{ -7.5, 200,
680 }`, versus `{ 0, 250, 600 }`); forces a re-fit; the aurora follows `center_x`.

- [ ] **Step 1: Failing tests** (independent projection, 4 window sizes): solo rows span 200-880 with the
  camera centred on x = -7.5; versus rows of both boards span 250-850, your board's projected centre is left
  of 800 and Jev's right of it, each board's projected columns lie inside its HUD cutout (Task 5 numbers).
- [ ] **Step 2-4:** FAIL, implement (`aim` takes x; `resize` uses the layout), PASS.

### Task 5: Versus HUD

**Files:** `tools/make_art.py` (`hud_versus.png`), `Assets/Scripts/prism_hud.lua`; Test `tests/entry_test.lua`,
`make_art.py check`.

**Interfaces:** `Hud:set_layout("solo" | "versus")`; `Hud:render(game, state, dt, race)` where `race` carries
`rival`, `jev` stats and `result`.

- [ ] **Step 1: Failing tests:** art check: both board cutouts transparent, cards opaque; entry test: in
  versus the overlay is `hud_versus.png`, each board's card shows its score, level and lines, both hold and
  next columns show their own game's pieces, the Jev line shows think time, calls and cost; solo-only widgets
  hidden in versus and back in solo; ready and solo-over screens offer PLAY and VS JEV; the race-over card
  names the winner (or draw) and both scores and lines and Jev's calls and cost, with REMATCH and SOLO.
- [ ] **Step 2-4:** FAIL, implement, PASS; show the overlay beside the solo one.

### Task 6: The race

**Files:** new `Assets/Scripts/prism_jev.lua`; `Assets/Scripts/prism.lua`; `Assets/Scripts/tetris_agent.lua`;
Tests `tests/entry_test.lua` (with a `proc` stub), `tests/agent_test.lua`.

**Interfaces:** `Jev.new{ port }`, `:start()` -> ok, error; `:update()`; `:stop()`; fields `running`,
`think_ms`, `calls`, `input_tokens`, `cost`, `error`, `finished`. `prism.versus()` starts a race;
`prism.rival`, `prism.rival_board`, `prism.jev`. Agent options take `game` as a getter.

- [ ] **Step 1: Failing tests:**
  - V (or the VS JEV button) starts a race: both games restart from one seed with equal active and next
    pieces, Jev's board shown, layout versus, the driver started once with exactly the Global Constraints
    arguments.
  - Your keys move only your game, never clear agent mode; a Jev placement moves only Jev's game; gravity
    runs on both; only your seat plays sounds.
  - You top out: race over, Jev wins, the adapter observes terminal, the best score unchanged; Jev tops out:
    you win; both in one frame: draw.
  - Driver lines update think time, calls, tokens and cost; `Finished:` marks it done; a failure line or an
    early exit ends the race with that message; `proc.start` failing ends it at once; after the race the
    driver is stopped within 3 s if it has not exited.
  - Pause stops both games and Jev's placement; resume continues; R restarts the race; Enter on the results
    card goes back to solo with Jev's board hidden.
  - Solo behaviour unchanged (takeover test in agent_test still passes).
- [ ] **Step 2-4:** FAIL, implement, all Lua suites PASS.

### Task 7: Live race

- [ ] **Step 1:** Quarantine count, then `run.ps1 -Port <free>`, start the race with `execute_lua
  "prism.versus()"`; mid-race and results-card screenshots at gameplay scale beside the solo shot; you are
  on the left.
- [ ] **Step 2:** Present mode immediate per the CLAUDE.md recipe; fps during the race (worst case) and the
  frame cost of Jev's per-piece placement build.
- [ ] **Step 3:** Quarantine count after; Player log clean; driver trace finished; Players closed.
