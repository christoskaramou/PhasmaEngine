# PRISM Showcase — Design Spec

Date: 2026-10-15. Status: approved in chat by the user; not committed (repo rule: commit only on request).

## Goal

Turn PRISM Tetris (`Games/PrismTetris`) from a flat 2D `runtime_ui` game into a showcase of the engine's
3D renderer, and add three features:

1. **Crystal-aurora graphics**: real 3D glass-like blocks, lighting, reflections, bloom and particles.
2. **Guideline speed**: the official guideline gravity curve; the agent board plays under real gravity.
3. **You vs Jev**: a side-by-side race on identical pieces, started from inside the game.
4. **Jev key, Harness-style**: paste a key in-game, see "saved", remove it.

Controls, scoring, hold, five-piece preview, the agent protocol (`game_describe/observe/act/reset`) and
the existing tests keep working.

## User decisions (2026-10-15)

| Topic | Decision |
| --- | --- |
| Look | Crystal aurora: glass-like 3D blocks glowing in their colour, mirror-dark stage, aurora sky, crystal-shatter particles |
| Versus | Race, same pieces; each board has its own level and speed; first to top out loses |
| Speed | Modern guideline curve, level every 10 lines |
| Jev key | Like Phasma Harness: paste into a key field, stored encrypted per Windows user, "saved" status with Remove. Storage is the Windows Credential Manager web credential `jev` / `api-key`, the entry the Harness test pool and skills already use |
| Re-decides | If a piece locks before Jev answers, Jev decides again on the next piece (one extra paid call, about $0.00013) |

Out of scope: garbage lines, music-reactive effects, T-spin scoring, reading the Harness app's own
`jev-key.enc`.

## Verified engine facts (spike and source, 2026-10-15)

- **Post-processing needs a Scene Settings node.** Without one, HDR, bloom, tonemapping, SSR and TAA are
  forced off (`Settings.cpp:41-42`). `node:set_scene_settings(true)` on an empty node enables them. Spike:
  HDR + ACES tonemapping + bloom + SSR + TAA rendered in the Player; glossy-floor reflections visible.
- **HUD stays crisp.** `runtime_ui` text drew sharp over HDR bloom in the spike.
- **Frame rate.** Spike scene in the hidden Player at 2560x1369: 150-180 fps.
- **Emissive saturates.** Emissive is unbounded (`vec3` magnitude is strength). ACES turned emissive
  `1.2 x hue` into white with a coloured rim; keep block emissive at or below about `0.6 x hue`.
- **Camera handedness.** With the camera at -z looking at +z, +x appeared on the left. Board axes are set
  empirically in the renderer milestone.
- **Pool everything.** Spawning or removing nodes rebuilds instances (`scene-prefabs.md`). The first
  `material.set` on a mesh creates its material instance and drains the GPU (`scene-scripts.md`). So all
  nodes are created and all materials set once at load; play only moves, shows, hides and re-colours.
- **Batch.** `scene.set_positions({node,x,y,z,...}, n)` and `material.set_emissives({node,r,g,b,...}, n)`.
- **Lights follow their node.** Light position is copied from the light's node every frame; move the node
  (`scene.find_model(light.name)`). Colour, intensity and radius stick.
- **Particles.** `particles.emit_burst{...}` for one-shots, `particles.add_emitter{...}` for long-lived
  emitters at fixed world positions; additive blend; 15 custom textures; `spawn_rate` is a per-slot chance.
- **Lua sandbox.** No `io`/`os`. `proc.start(path, args)` runs one child at a time, executable under
  `Assets/`, no `.bat`/`.cmd`, stdout via `proc.read_line()`. No text input field and no clipboard access.
- **Known engine bug.** Iterating `pairs()` over every global table crashes the Player (separate task).

## Architecture

### Lua modules (`Games/PrismTetris/Assets/Scripts`)

| Module | Responsibility | Status |
| --- | --- | --- |
| `tetris_rules.lua` | Rules; guideline gravity and 20G | changed |
| `prism_stage.lua` | Scene settings node and post settings, camera rig (frame, sway, shake), key light, floor, back glass, aurora ribbons, ambient motes, particle textures, bloom pulse | new |
| `prism_board3d.lua` | One instance per board: pooled cubes (200 cells, 4 active, 4 ghost), frame bars, per-board piece light and flash lights, per-kind materials, diff sync from game state, impact effects | new |
| `prism_hud.lua` | `runtime_ui` overlay from `tetris_view.lua`: score, level, lines, hold, next, toasts, menus, results card, key panel; solo and versus layouts | replaces `tetris_view.lua` |
| `prism_jev.lua` | Runs `Bots/PhasmaGameDriver.exe` through `proc`: versus match process, key status/save/remove, parses output lines | new |
| `tetris_agent.lua` | Agent adapter: targets one board; real-time state key, re-plan at act time, timed execution with `busy` | changed |
| `prism.lua` | Orchestration: modes (ready, playing, paused, over), solo or versus, input routing, events to HUD and boards | changed |

`prism.lua` keeps exposing `_G.prism` (game, state, view, start, restart, apply, agent) for tests and
tooling; `view` becomes the HUD plus boards.

### GameDriver (`Phasma/GameDriver`, C++)

- `--realtime`: `stale_state` and `busy` tool errors, and an observation with no actions while not
  terminal, are waited out (re-observe every 50 ms) instead of ending the run. Invalid or illegal actions,
  protocol violations and Jev response errors still end it. The run ends on terminal or when stopped.
- Key commands (no game connection, no network):
  - `--key-status` prints `KEY saved` or `KEY missing`.
  - `--save-key-from-clipboard` reads `CF_UNICODETEXT` in-process, trims it, validates it (1-4096
    printable ASCII, no spaces) and replaces the `jev`/`api-key` web credential. Prints `KEY saved`,
    `KEY invalid` or `KEY clipboard-empty`. The clipboard is left unchanged.
  - `--remove-key` prints `KEY removed` or `KEY missing`.
  - The key is never printed, logged or accepted on the command line.

### Launch

`Games/PrismTetris/run.ps1` moves from an editor copy to a private Player runtime (like
`Phasma/GameDriver/run-tetris.ps1`): game assets copied into `build-prism-tetris`, MCP enabled on port
8876 in the runtime copy's `Assets/Agent/agent_config.json`, `PhasmaGameDriver.exe` copied into the
runtime `Assets/Bots/`, window shown on display 1. Save data in the runtime copy is preserved between
launches. One PRISM Player at a time (both launchers use port 8876).

## Visual design: crystal aurora

### Geometry and camera

- Cell pitch 1.0 world unit; cube size 0.9; 10 x 20 visible cells per board.
- Solo: one board centred. Versus: boards at x = -7.5 (you) and +7.5 (Jev), camera pulled back.
- Perspective camera, horizontal FOV about 40 degrees, slightly above the board centre, looking at it.
- Idle sway: +-0.15 units over 10-14 s. Shake: 0.08 on hard drop, 0.25 on a 4-line PRISM, decaying in
  0.35 s. Reduced FX (F1) disables sway and shake.

### Blocks

| Kind | Hue (linear RGB) |
| --- | --- |
| I | 0.25, 0.85, 1.00 |
| O | 1.00, 0.78, 0.25 |
| T | 0.68, 0.42, 1.00 |
| S | 0.35, 0.95, 0.60 |
| Z | 1.00, 0.35, 0.55 |
| J | 0.30, 0.50, 1.00 |
| L | 1.00, 0.55, 0.25 |

- Settled block: base colour = hue, roughness 0.18, metallic 0, emissive = hue x 0.30.
- Active piece: emissive = hue x 0.55, pulsing +-0.12 at 2 Hz.
- Ghost: dark glass, emissive = hue x 0.10.
- Glass look: try the `transmission` render type in milestone 1; if it reads poorly, keep the glossy
  emissive look above.

### Stage

- Floor: large plane under the boards, dark navy, roughness 0.08, metallic 0.6 (SSR reflections).
- Back glass: very dark panel behind each board with a faint emissive cell grid.
- Frame: thin bars around each well, cool white emissive 0.35.
- Aurora: three to four large quads far behind, black base, slowly breathing coloured emissive.
- Ambient: one long-lived motes emitter drifting across the stage.
- Post: HDR, ACES tonemapping, bloom (strength about 2.5, range about 2), SSR, TAA, light colour
  grading (saturation 1.1, contrast 1.05), light fog.

### Lights

- One cool directional key light.
- Per board: four cell-sized area lights in the active piece's hue, one in front of each of its cells and
  facing the board, so the light has the piece's shape (intensity 2, range 5; user choice 2026-10-15,
  replacing a point light whose highlight read as a hot spot).
- Per board: two pooled flash point lights for line clears (white to hue, decaying in 0.25 s).

### Effects (particles from `emit_burst`; textures generated by `tools/make_art.py`)

| Event | Effect |
| --- | --- |
| Line clear | Crystal-shard burst from each cleared cell (hue to white, outward and up, gravity), light sweep along the row, flash light |
| Hard drop | Sparks under the landed cells, small shake |
| 4-line PRISM | Larger bursts, bloom pulse (+1.5 strength decaying), shake 0.25, toast |
| Level up | Ring burst at the board centre, aurora hue shift |
| Top out | Board blocks fade to dark over 1 s |
| Reduced FX | No shake or flash lights, particle counts x 0.3, no bloom pulse |

### HUD

Same information as today on glass cards over the 3D view. Solo: hold and stats on the left, next five on
the right, brand top-left. Versus: a compact card above each board (score, level, lines), Jev's last think
time and running cost estimate. Menus: SOLO, VS JEV, JEV KEY. Results card at the end of a race.

## Speed

- Gravity interval for level L below 20: `(0.8 - (L - 1) * 0.007) ^ (L - 1)` seconds per row
  (1.0 s at L1, 0.793 at L2, about 0.064 at L10).
- Level 20 and above: 20G, the piece drops to its landing row on spawn and on each tick; lock delay
  (0.5 s, 15 resets) is unchanged.
- Soft drop keeps today's rule (`min(interval, 0.035)` seconds per row).
- Level = floor(lines / 10) + 1 (unchanged).

## Real-time agent

- The agent board ticks gravity every frame like the human board (no freeze).
- `state_key` covers the board, active kind, rotation and column, hold, `can_hold`, next queue and mode;
  it excludes the active row and timers, so a falling piece does not invalidate a decision.
- Act: the chosen placement's landing is re-planned from the piece's current position. If reachable, its
  inputs run one every 0.07 s (a fast human), then a hard drop; the adapter reports `busy` until done. If
  not reachable, nothing runs and the agent is asked again (counts as a re-decide).
- Pause: both boards stop; the agent sees no actions and the realtime driver waits.
- In versus the agent only ever drives the right board; human keys drive the left board only.

## Versus race

1. VS JEV checks the key (`--key-status`); if missing, the key panel opens.
2. One seed for both boards: identical piece order. Each board has its own level and speed.
3. The game starts the driver: `Bots/PhasmaGameDriver.exe --url http://127.0.0.1:<mcp_port>/mcp
   --policy jev --key-source vault --steps 10000 --realtime` (port read from the runtime's
   `agent_config.json`).
4. The HUD shows Jev's last decision time (parsed from the driver's step lines) and an estimated cost.
5. The first board to top out loses; the race stops, the driver is stopped, the results card shows both
   scores and lines and Jev's calls and cost.
6. If the driver exits early (missing key, network, Jev error), the race ends with a clear message.

## Jev key panel

- Opened from JEV KEY or from VS JEV without a key.
- Status: "Saved for this Windows user" or "No key saved".
- A key field reading "Click here, then press Ctrl+V"; when saved it shows a masked placeholder.
  Ctrl+V or PASTE runs `--save-key-from-clipboard`; REMOVE runs `--remove-key`.
- Key commands only run when no race driver is running (one child process at a time).
- First use: compare the Bitdefender quarantine count (`C:\ProgramData\Bitdefender\Desktop\Quarantine`)
  before and after; report any change.

## Testing and verification

- Lua (luajit, engine stubs):
  - `rules_test.lua`: guideline intervals (L1, L2, L10) and 20G at L20.
  - `agent_test.lua`: falling keeps `state_id`; act re-plans from a lower row; `busy` during execution;
    unreachable plan runs nothing.
  - `entry_test.lua`: menus, solo and versus start (same next queue on both boards), key panel, reduced FX,
    resize, retained-widget reuse.
  - New `render_test.lua`: no node or material creation after load; batched sync call counts; effects
    honour reduced FX.
- GameDriver: `ProtocolTests.cpp` realtime handling (stale, busy, empty actions); `LauncherTests.ps1`
  key commands never print key material.
- Visual: Player MCP screenshots at gameplay scale per milestone, beside the "before" shot; the user
  judges.
- Performance: Release Player, `get_state` fps during heavy effects; target at least 120 fps at
  2560x1369 on this machine; report the worst case.
- Logs: `PhasmaEngine.log` warnings and errors after every run.

## Milestones

1. Renderer, solo play (stage, board, HUD restyle, effects).
2. Guideline speed, real-time agent, driver `--realtime`.
3. Versus race.
4. Jev key panel.

## Risks

- The transmission look may not read well; fallback is glossy emissive.
- Bitdefender's reaction to Player -> driver -> clipboard -> Credential Manager is untested.
- Frame rate with about 430 cubes plus particles is unmeasured.
- Port 8876 is shared with `run-tetris.ps1`; only one PRISM Player can run at a time.
