# PRISM Crystal-Aurora Renderer Implementation Plan (Milestone 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace PRISM's flat 2D board with a pooled 3D crystal board, aurora stage, lights, bloom and
particle effects, keeping every control, the agent protocol and the HUD information.

**Architecture:** Three Lua modules split the old `tetris_view.lua`: `prism_stage.lua` owns scene-wide
look (post settings, camera fitted to the HUD board cutout, key light, floor, aurora backdrop, motes,
shake, bloom pulse); `prism_board3d.lua` owns one board (200 pooled cell cubes, active and ghost cubes,
piece light, row sweeps, flash lights, bursts); `prism_hud.lua` is the old view minus the board, drawing a
transparent HUD overlay over the 3D frame. Everything is created and painted once at load.

**Tech Stack:** LuaJIT game scripts on PhasmaEngine bindings (`primitives`, `material`, `scene`, `lights`,
`particles`, `settings`, `get_camera`, `runtime_ui`); Python/Pillow/NumPy art generator; luajit tests with
engine stubs; PhasmaPlayer + MCP for screenshots.

**Spec:** `docs/superpowers/specs/2026-10-15-prism-showcase-design.md`

## Global Constraints

- Code layout follows the repo CLAUDE.md "Rules — code layout" in Lua and Python: one statement per line,
  blank lines between steps, wrap by operand, no code line over about 120 columns, LF line endings.
- No commits (repo rule: commit only when the user says so).
- All nodes are created and every material property is first set at load; play only moves, shows, hides,
  re-colours (`material.set` on an existing instance, `material.set_emissives`) and lights.
- Block emissive at most 0.6 x hue per channel (ACES turns stronger emissive white).
- Lights are moved through their node; never call `lights.set_rotation` (it wedged MCP dispatch before).
- Never iterate `pairs()` over engine binding globals (crashes the Player).
- `_G.prism` keeps `game`, `state`, `view`, `start`, `restart`, `apply`, `agent`; adds `board`, `stage`.
- Existing controls, scoring, agent protocol and `rules_test`/`agent_test` stay green.

## Review Focus

- Window aspect other than 16:10 (ultrawide 2560x1080, 4:3 1600x1200): the 3D board must stay inside the
  HUD's board cutout. Pinned by Task 1's fit test at three sizes.
- A piece partly in the hidden spawn rows: those cubes stay hidden and nothing is written for them. Pinned
  by Task 2's spawn test.
- Restart or New Game during a top-out fade or running effects: blocks come back at full glow, sweeps and
  flash lights stop. Pinned by Task 3's reset test.
- Turning effects off (F1) during a shake or bloom pulse: the camera returns to rest and bloom to its base
  value on the next frame. Pinned by Task 1's reduced-FX test.
- Many clears in quick succession: every impact emits a bounded number of bursts (at most 2 per row plus
  sparks), so particle emitters cannot pile up. Pinned by Task 3's burst-count test.

---

### Task 1: Engine stubs and stage

**Files:**
- Create: `Games/PrismTetris/tests/engine3d_stubs.lua`
- Create: `Games/PrismTetris/Assets/Scripts/prism_stage.lua`
- Create: `Games/PrismTetris/tests/render_test.lua`

**Interfaces:**
- Produces `Stage.new() -> stage`, `stage:resize(width, height)`, `stage:update(dt, motion)`,
  `stage:jolt(strength)`, `stage:flare()`, `stage:shift(level)`, `stage:point_light() -> light`,
  `stage:set_light(light, {x,y,z}, {r,g,b}, intensity, radius)`, fields `stage.side` (+1/-1, camera z side),
  `stage.textures.glow|shard|spark`, `stage.eye`, `stage.target`, `stage.shake`.
- Produces `engine3d_stubs.lua` returning a `record` with `nodes`, `bursts`, `emitters`, `settings`,
  `material_sets`, `frozen`, `created_while_frozen`.

- [ ] **Step 1: Write the stubs** (`tests/engine3d_stubs.lua`) installing globals `vec3`, `vec4`,
  `primitives`, `material`, `scene`, `lights`, `particles`, `settings`, `get_camera` that record calls,
  count node creation while `record.frozen` is true, and model point/directional lights with names and
  nodes found through `scene.find_model`.
- [ ] **Step 2: Write failing stage tests** in `tests/render_test.lua`: post settings applied
  (`hdr`, `bloom`, `tonemapping`, `ssr`, `taa` true), one scene-settings node, camera fit at 1600x1000
  (target y about 11.18, eye distance about 56.8) and the projected board height fraction within 1% of
  `680 * scale / height` at 1600x1000, 2560x1080 and 1600x1200; `jolt` + `update(dt, false)` leaves the
  camera at `eye`; `flare` + `update(dt, false)` restores `bloom_strength` to 2.5.
- [ ] **Step 3: Run** `build-ninja-physics/Release/luajit.exe Games/PrismTetris/tests/render_test.lua`;
  expected FAIL (missing `prism_stage.lua`).
- [ ] **Step 4: Implement `prism_stage.lua`** as specified in the spec's Stage section: `Stage.new` applies
  POST settings on a `scene.add_empty_node("PrismSceneSettings")` with `set_scene_settings(true)`, takes
  `get_camera()` to perspective with 45 degree horizontal FOV, fits it in `resize` with
  `view_height = 20 * height / (680 * scale)`, `distance = view_height / 2 / tan(half_vertical)`,
  `offset = (center_px - height / 2) / height * view_height`, adds the key directional light (rotated via
  its node), the floor plane, the aurora backdrop cube with `Textures/aurora.png` as its emissive texture,
  particle textures and one long-lived motes emitter; `update` runs sway, shake, bloom pulse and aurora
  breathing/tint.
- [ ] **Step 5: Run the test; expected PASS.**

### Task 2: Board cells and piece

**Files:**
- Create: `Games/PrismTetris/Assets/Scripts/prism_board3d.lua`
- Modify: `Games/PrismTetris/tests/render_test.lua`

**Interfaces:**
- Consumes the Task 1 stage API.
- Produces `Board.new({ rules, stage, origin = {x,y,z} }) -> board`, `board:sync(game, mode)`,
  `board:update(dt, motion)`, `board:impact(event, motion)`, `board:reset()`, `board:clear_effects()`,
  fields `board.cells[1..200]`, `board.kinds[1..200]`, `board.active[1..4]`, `board.ghost[1..4]`,
  `board.flashes[1..2]` (`life`), `board.sweeps[1..4]` (`life`), `board.fade`, `board.fading`.

- [ ] **Step 1: Write failing tests:** after `Stage.new` + `Board.new` set `record.frozen = true`; ready mode
  shows the decorative pattern (cell 191 visible); a fresh game hides all settled cells; a piece at spawn
  (`y = 1`) keeps its cubes in the hidden rows invisible; after soft drops to `y >= 3` all four active cubes
  are visible and positioned in world space (`x - 5.5`, `22 - y + 0.5`); ghost hidden when the piece sits
  on its landing row; `material.set` after load only touches `base_color` of changed cells.
- [ ] **Step 2: Run; expected FAIL.**
- [ ] **Step 3: Implement** pooled cubes (`primitives.cube(0.9)`), per-kind base colours
  (hue x 0.35) prebuilt as `vec4`, settled emissive hue x 0.30 via `material.set_emissives`, active
  emissive hue x (0.55 +- 0.12 at 2 Hz), ghost emissive hue x 0.10, positions via `scene.set_positions`,
  piece light following the piece centroid 1.6 units toward the camera; back glass, grid bars and frame
  bars built at load.
- [ ] **Step 4: Run; expected PASS.**

### Task 3: Board effects

**Files:**
- Modify: `Games/PrismTetris/Assets/Scripts/prism_board3d.lua`
- Modify: `Games/PrismTetris/tools/make_art.py` (aurora, hud, shard, spark textures)
- Modify: `Games/PrismTetris/tests/render_test.lua`

- [ ] **Step 1: Write failing tests:** hard drop emits one spark burst at the landing cells and jolts the
  stage by 0.08; a one-line clear emits two shard bursts, starts one sweep and one flash light; four lines
  also flare bloom and jolt 0.25; reduced FX emits bursts with `count <= 11`, starts no flash light and no
  jolt; every impact emits at most `2 * rows + 1` bursts; `update(0.3, true)` ends sweeps and flashes;
  `game_over` fades emissive to 0.12 within 1 s and `reset` restores hue x 0.30; a level increase emits one
  burst and sets the stage tint target.
- [ ] **Step 2: Run; expected FAIL.**
- [ ] **Step 3: Implement** `impact`, `clear_row`, `sparks`, `level_burst`, sweep and flash pools and fade
  per the spec's effects table; generate `aurora.png` (old atmosphere + sky), `hud.png` (transparent
  header, cards, controls, footer), `shard.png` and `spark.png` in `make_art.py`, drop `backdrop.png` and
  `ghost.png`.
- [ ] **Step 4: Run `make_art.py`, then the tests; expected PASS.**

### Task 4: HUD and wiring

**Files:**
- Create: `Games/PrismTetris/Assets/Scripts/prism_hud.lua` (from `tetris_view.lua`, minus board,
  particles and flashes; `hud.png` overlay instead of the backdrop and letterbox fill)
- Delete: `Games/PrismTetris/Assets/Scripts/tetris_view.lua`
- Modify: `Games/PrismTetris/Assets/Scripts/prism.lua`
- Modify: `Games/PrismTetris/tests/entry_test.lua`

- [ ] **Step 1: Update `entry_test.lua`:** install `engine3d_stubs.lua` first; `widgets.hud.path ==
  "Textures/hud.png"`; ready pattern via `prism.board.cells[191].visible`; start clears it; reduced-FX
  impact keeps the toast, starts no flash light and emits only small bursts; resize keeps `hud` at the
  surface size; idle paused frames make no `runtime_ui` calls and create no nodes.
- [ ] **Step 2: Run entry, agent and render tests; expected FAIL.**
- [ ] **Step 3: Implement** `prism_hud.lua` and wire `prism.lua`: create stage, board and HUD; `feedback`
  calls HUD and board impact; render calls HUD render, `board:sync`, `board:update`, `stage:resize` (on
  surface change) and `stage:update`; New Game resets the board; the F1 toggle clears board effects.
- [ ] **Step 4: Run all four Lua test files; expected PASS.**

### Task 5: Live verification and tuning

- [ ] **Step 1:** Launch a visible Player with the game (`Phasma/GameDriver/run-tetris.ps1 -Policy local
  -Steps 40 -Seed 42 -PaceMs 300 -ShowWindow`), screenshot through Player MCP `take_screenshot`: ready
  screen, mid-game, a line clear, a four-line clear.
- [ ] **Step 2:** Check the headline requirements in each shot at gameplay scale: board inside the HUD
  cutout, columns left-to-right correct (flip `stage.side` if mirrored), colours read as their piece hue,
  HUD text crisp; tune constants only.
- [ ] **Step 3:** Try `material.set_render_type(node, "transmission")` on cell cubes for the glass look;
  keep it only if the screenshot reads better.
- [ ] **Step 4:** Measure `get_state` fps during clears in the Release Player at 2560x1369; target at least
  120 fps; scan `PhasmaEngine.log` for warnings and errors.
- [ ] **Step 5:** Show the shots beside `prism_before.png`; the user judges.

## Next plans

Milestones 2-4 of the spec (guideline speed and real-time agent, versus race, Jev key panel) each get
their own plan after this milestone lands, because they build on the board and HUD interfaces above.
