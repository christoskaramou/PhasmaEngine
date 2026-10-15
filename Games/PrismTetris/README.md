# PRISM Tetris

A native PhasmaEngine game: midnight aurora, crystal pieces, ghost landing,
hold, five-piece preview, wall kicks, combos, levels, particles and original
synthesized sound effects. No browser, network service or engine rebuild required.

## Play

Run `Games/PrismTetris/run.cmd`, or:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Games/PrismTetris/run.ps1
```

The launcher copies the Release Player from `build-ninja-physics/Release` into
`build-prism-tetris/player`, with the game's assets, MCP on port 8876 (`-Port N`
changes it) and the race driver (`build-game-driver/Release/PhasmaGameDriver.exe`)
in `Assets/Bots`. The best score in that copy's `Assets/Save` stays between
launches. A second launch reuses the running game; `Phasma/GameDriver/run-tetris.ps1`
also uses port 8876, so run one at a time. The window opens on display 1. Enter
begins play.

| Key | Action |
| --- | --- |
| Left / Right or A / D | Move; hold to repeat |
| Down or S | Soft drop |
| Up or X / Z | Rotate clockwise / counterclockwise |
| Right click / Left click | Rotate clockwise / counterclockwise |
| Space | Hard drop |
| C or Left Shift | Hold / swap, once per piece |
| P | Menu (pause) / resume |
| Ctrl+V | Paste the Jev key (in the Jev key panel) |
| Enter | Start / resume; solo after a race |
| V | Race Jev (from the menu, a finished game or pause) |
| R | New game; new race during a race |
| M | Mute sound |
| F1 | Effects on / off (particles, sweeps, flashes, shake, sway) |

Buttons also support starting, pausing, racing, sound and effects. Best score persists
locally in `Assets/Save/prism_best.txt`. Scoring includes line clears, combos,
back-to-back four-line clears, and soft/hard drops; T-spin scoring is not included.

## Menu

MENU (top right, or P) pauses and opens the menu: RESUME, SOLO (a fresh solo
game), VS JEV (a race) and JEV KEY. The start screen and game-over card offer
PLAY, VS JEV and JEV KEY; the race results card offers REMATCH, SOLO and JEV KEY.
The key cannot change while a race is running.

## Race Jev

VS JEV (or V) starts a race on identical pieces: your board on the left, Jev's on
the right, each with its own level and speed. Your keys drive only your board. The
first board to top out loses (both in one frame is a draw), and the results card
shows both runs and Jev's calls and cost; V rematches, Enter returns to solo.
Races never change your best score.

The game starts `Assets/Bots/PhasmaGameDriver.exe` itself (`run.ps1` copies it
there from `build-game-driver/Release`) against its own MCP port. Each Jev decision
is a paid TypeSafe call (about $0.00013); the HUD shows think time, calls and the
running cost. Without a saved key the race ends with the driver's message.

## Jev key

JEV KEY shows whether a key is saved. Copy your Jev key, then PASTE KEY (or
Ctrl+V): the driver reads the clipboard itself, trims it, checks it is 1-4096
printable characters with no spaces, and stores it as the Windows Credential
Manager web credential `jev` / `api-key`, the one the Harness and skills use. The
key is never shown, logged or put on a command line, and the clipboard is left as
it was. REMOVE deletes the saved key.

## Agent play

`Phasma/GameDriver` contains the separate reusable Windows executable and its
build instructions. Its launcher opens PRISM in a private Player on port 8876:

```powershell
./Phasma/GameDriver/run-tetris.ps1 -Policy local -Steps 50 -Seed 42
```

`local` is the offline baseline. Select `-Policy jev` to make paid TypeSafe
requests using `JEV_API_KEY` or `TYPESAFE_API_KEY` from the environment. The
piece keeps falling while the agent decides, and its placement plays out as one
input every 0.07 s; gameplay keys resume human control at once, and the agent gets
no placements again until the driver resets the game. Assisted runs
never replace the human best score, including after takeover. New Game starts
a fresh manual run.

## Checks

```powershell
build-ninja-physics/Release/luajit.exe Games/PrismTetris/tests/rules_test.lua
build-ninja-physics/Release/luajit.exe Games/PrismTetris/tests/entry_test.lua
build-ninja-physics/Release/luajit.exe Games/PrismTetris/tests/agent_test.lua
build-ninja-physics/Release/luajit.exe Games/PrismTetris/tests/render_test.lua
```

Tests cover rules, controls, pause, score persistence, reduced effects, resizing
and retained UI reuse. To regenerate the artwork, run `tools/make_art.py` with
Python, Pillow and NumPy. Sound generation (`tools/make_audio.py`) uses only the
Python standard library. Generated assets are included, so neither is needed to play.

## Editing

The scene is `Assets/Scenes/prism_tetris.pescene`; its entry script loads the
rules, `prism_stage.lua` (post effects, camera, aurora), `prism_board3d.lua`
(the 3D crystal board and its effects) and `prism_hud.lua` (the retained runtime UI
over `hud.png`). Every 3D node and material is created at load; play only moves,
shows, hides and re-colours them.

`run.ps1` copies the scripts into the Player's runtime on every launch, so close
PRISM and run it again to pick up edits. The running Player answers MCP on its port
(`POST http://127.0.0.1:8876/tool` with `{"name": "execute_lua", ...}` or
`take_screenshot`), and `_G.prism` exposes the games, boards, HUD, stage, race,
`versus()` and the agent for tooling.
