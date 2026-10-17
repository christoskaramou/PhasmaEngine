# Phasma Game Driver

A separate Windows console executable drives a running PhasmaPlayer through its
existing local MCP server. Games supply legal actions and observations; Jev chooses
an action ID. No game rules or model credentials live in the driver protocol.

## Build and play Tetris

Run from the repository root in PowerShell with Visual Studio C++ tools, CMake and
Python installed. The Player builder reuses this workspace's cached engine dependencies.

```powershell
./Phasma/GameDriver/build.ps1
../PhasmaProjects/Arcade/PrismTetris/run-race.ps1 -Policy local -Steps 50
```

The driver takes no project path: it attaches to a running Player's MCP server (`--url`,
default `http://127.0.0.1:8876/mcp`; `--seed` resets the game, omitting it attaches). The PRISM
launcher, `run-race.ps1`, lives with the game in PhasmaProjects (it replaced this folder's
`run-tetris.ps1` on 2026-10-17): it starts or reuses PRISM with the game's `run.ps1` (Release Player from
`build-ninja-physics`, MCP port 8876), then runs `build-game-driver/Release/PhasmaGameDriver.exe`.
Both find this engine by searching upward for a `PhasmaEngine` folder (or `PHASMA_ENGINE`). The window
stays open after play; an occupied port causes a clear error; `-Port 8877` selects another. Editor settings and human saved scores stay separate.

`local` is an offline numeric baseline, **not Jev**. It requires adapter-provided
numeric action scores. The live native Tetris smoke completed 50 placements,
18 lines and 4,896 points with seed 42 and zero API calls.

To use Jev, provide `JEV_API_KEY` or `TYPESAFE_API_KEY` explicitly in the terminal
that launches it. In PowerShell 7, enter it with masked input:

```powershell
$env:JEV_API_KEY = Read-Host 'Jev API key' -MaskInput
try {
    ../PhasmaProjects/Arcade/PrismTetris/run-race.ps1 -Policy jev -Steps 20 -Seed 42
} finally {
    Remove-Item Env:JEV_API_KEY -ErrorAction SilentlyContinue
}
```

Enter the key locally; do not paste it into chat. By default the launcher and
executable read only those environment variables, never browser credentials or
another app's configuration.

`-UseVault` (`--key-source vault` on the executable) instead reads the Credential
Manager Web Credential `jev` / `api-key` (the entry `set-key` saves) inside the
driver process with WinRT `PasswordVault`: no child process, nothing printed.
It is opt-in. Bitdefender left it alone in the 2026-10-15 PRISM races (quarantine
count unchanged); its reaction to `--save-key-from-clipboard` and `--remove-key` is unverified.

```powershell
../PhasmaProjects/Arcade/PrismTetris/run-race.ps1 -Policy jev -UseVault -Steps 20 -Seed 42
```

Missing/invalid credentials fail before connecting to or resetting the game.

Key commands run alone (no other argument, no game connection, no network) and
print one line, never the key: `--key-status` (`KEY saved` / `KEY missing`),
`--save-key-from-clipboard` (reads Unicode clipboard text in-process, trims it,
requires 1-4096 printable characters without spaces, replaces the `jev` /
`api-key` web credential and leaves the clipboard unchanged: `KEY saved`,
`KEY invalid` or `KEY clipboard-empty`) and `--remove-key` (`KEY removed` /
`KEY missing`); any failure prints `KEY error`. Saving first adds the credential over
the old one and reads it back; only if the vault kept the old key does it remove
and re-add, holding the old credential to put back if the add fails. PRISM's JEV KEY panel runs these.

`-PaceMs N` (`--pace-ms N`, 0..10000, default 0) makes each placement take at least
N ms, counted from the start of its decision, so play looks human. A turn-based game
waits between decisions, so pacing does not change its gameplay; speed summaries
then measure the pace, not the policy. A real-time game (PRISM) keeps running
through the wait, so the driver observes again before the next decision. At high
levels a long pace lets pieces lock before the agent reacts; that is the cost of
the setting. The Player window is hidden unless you pass `-ShowWindow`.
New Player processes do not inherit the model keys; only the driver receives them.

This makes up to 20 paid requests to TypeSafe's `jev-1.13.0`. The executable
requires an explicit policy. Ctrl+C cancels; network calls have bounded deadlines
and no automatic retries. JSONL traces record decisions, scores, latency and token
counts under the private runtime. They contain observations, never API keys.
Real Jev gameplay quality, latency and cost have not been measured.

The launcher also writes a `.summary.json` beside each completed trace, with
placements per second, mean/p95 request latency, first-request latency and token
counts. Its elapsed time covers the driver play loop and excludes Player startup.
An incomplete run is not reported as a speed measurement.

On 2026-10-15 an ad-hoc benchmark imported a profiler helper that retrieved a
saved key through Python and PowerShell's Windows PasswordVault API. Bitdefender
flagged that process chain. That helper is not part of this launch path; use the
command above for Jev tests. Security settings and exclusions are not changed.
The alert does not establish whether every application listed was infected;
any remaining detections need their exact file/detection details reviewed.

## Connect to another game

Enable MCP in that project's `Assets/Agent/agent_config.json` before launching Player:

```json
{"mcp": true, "mcp_port": 8877}
```

Connect without resetting the current session:

```powershell
./build-game-driver/Release/PhasmaGameDriver.exe --url http://127.0.0.1:8877/mcp --policy jev --steps 20
```

Add `--seed 42` to explicitly reset, or `--trace <path>` to choose the JSONL file.
Only loopback HTTP game endpoints are accepted. Missing `mcp_port` keeps the
Player's existing default 8765; the editor's MCP listener is unchanged.

## Add a game adapter

New projects generated with `tools/new_game.py` already include the shared
GameKit interface, bounded input injection and lifecycle cleanup. The current
topdown template offers 12-frame movement/wait actions through its normal input
controller. Existing games implement three callbacks:

```lua
local kit = module("Scripts/gamekit/init.lua")
local driver = kit.driver.new({
    id = "my_game",
    description = "Describe the objective and public controls.",
    observe = function()
        return {
            mode = "playing", terminal = false, score = 0,
            observation = { player = { x = 0, y = 0 } },
            actions = { { id = "left", description = "Move left for 12 frames." } },
        }
    end,
    act = function(action_id)
        -- Route to the same controller used by physical input.
    end,
    reset = function(seed)
        -- Reset through the game's existing controller.
    end,
})
hooks({
    init = function() driver:attach() end,
    destroy = function() driver:detach() end,
})
```

A C++ game (ProjectNative) gets the same four tools from
[GameKit.h](../ProjectNative/Api/GameKit.h) (ABI 29); [CoinGrid](../ProjectNative/Sample/CoinGrid.cpp)
is a complete one:

```cpp
#include "GameKit.h"
#include "ScriptModule.h"

class MyGame
{
public:
    MyGame(const phasma::ScriptApi &api, phasma::Node) : driver(api, Spec()) {}
    void Update(double) {}

private:
    phasma::gamekit::Game Spec()
    {
        phasma::gamekit::Game game;
        game.id = "my_game";
        game.description = "Describe the objective and public controls.";
        game.observe = [this](phasma::gamekit::State &state)
        {
            state.mode = "playing";
            state.observation = "{\"player\":{\"x\":0,\"y\":0}}"; // JSON, written in a fixed order
            state.actions = {{"left", "Move left one cell.", 0.0}};
        };
        game.act = [this](const std::string &actionId)
        {
            // Route to the same controller physical input uses.
        };
        game.reset = [this](uint32_t seed)
        {
            // Reset through the game's existing controller.
        };
        return game;
    }

    phasma::gamekit::Controller driver; // last: its callbacks use the members above
};
PHASMA_NODE_SCRIPT(MyGame)
```

Attach it to a node (`cpp:MyGame`) and run the Player with MCP enabled. The Controller registers the
tools while the script instance lives; the engine removes them when the instance stops and before a
module reload, so a driver connected across a reload stops with `stale_state` (start it again).

`game_describe`, `game_observe`, `game_act` and `game_reset` use protocol version 1.
Observations include game/session/state IDs, mode, score, terminal/busy flags,
public state and legal named actions. Requests carry session/state IDs; stale,
duplicate, malformed and unavailable actions fail before gameplay callbacks.
Reset seeds are integers from 0 through 2147483647. Each reset starts a new session.
Keep action IDs deterministic, observations compact and hidden information out.

For actions spanning frames, return `busy=true` until completion. The driver
polls until ready, for at most 30 seconds. A real-time adapter may provide
`state_key(public_state)` containing only the logical conditions that invalidate
a pending intent, such as HP, wave and enemy identities. Otherwise every public
state change invalidates the previous observation.

A game with `state_key` describes itself as `state_tracking: "game_epoch"`, and
the driver then treats it as real-time: it waits out `busy`, pauses and empty
action lists with no deadline (Ctrl+C still cancels), and when an act comes back
`stale_state` or `busy` it writes a `redecide` trace row, observes again and
decides again. Re-decides are counted in the finish row and the `Finished:` line;
each one is another paid Jev call, so `--steps` caps decisions and re-decides
together. Every other rejection still ends the run. Step lines read
`<model> step <n> action=<id> score=<s> decision_ms=<ms> input_tokens=<t>` and
re-decide lines end in `input_tokens=<t>`, so a game that starts the driver itself
(PRISM's race) can show think time and running cost.

Tetris enumerates reachable placements (including hold) and computes numeric board
consequences locally. The piece keeps falling under guideline gravity while the
agent decides; its state key leaves out the falling row, so gravity alone never
expires a decision. The chosen landing plays out as one input every 0.07 s
through the shared controls, re-planned from wherever the piece has fallen, and
the game reports `busy` until the drop. A landing that becomes unreachable is
abandoned and the piece keeps falling. Physical gameplay input resumes human
control at once, and the human keeps it: after a gameplay key or New Game the
game offers no actions until the next `game_reset`, so a running driver just
waits (Ctrl+C stops it), and attaching without `--seed` to a manual game waits
the same way. General real-time games still run on real engine ticks: this
interface does not fast-forward physics or rendering.

## Verification

`build.ps1` runs the protocol fixtures and localhost mock MCP tests. No model
calls are made. `tests/RealtimeLoopTests.ps1` (`game-driver-realtime`) serves a
scripted game over a loopback MCP endpoint to the real executable and checks the
step cap with re-decides, pacing, and stale and rejected acts. Additional
deterministic game checks:

```powershell
python tools/tests/test_game_driver.py
./build-ninja-physics/Release/luajit.exe ../PhasmaProjects/Arcade/PrismTetris/tests/agent_test.lua
```

The GameKit check requires the existing Python `lupa` package and exercises the
actual generated topdown script with engine stubs. The C++ GameKit is covered by
`tools/tests/native-scripts` (`CheckGameKit`) and, against a real Player, by
`python tools/tests/native-scripts/driver_smoke.py --binary-dir <Release folder> --driver build-game-driver/Release/PhasmaGameDriver.exe`
(see the [ProjectNative README](../ProjectNative/README.md)). Production transport uses
Windows WinHTTP and the repository's existing JSON header; no dependencies are added.
