# Player, export, and the game driver

## Player

`PhasmaPlayer` runs a project with no editor panels, no gizmos, and no scene authoring. The game draws its own picture and its own HUD. Start it from the launcher's **Run → Phasma Player**, or run the executable yourself from the build folder. It reads the same `phasma_settings.json` as the editor: project root, manifest, and startup scene.

Useful arguments (the launcher's **Parameters** field appends these):

| Argument | What it does |
|---|---|
| `--api vulkan` or `--api dx12` | Graphics backend. DX12 is Windows-only. |
| `--display <index>` / `--screen <index>` | Monitor to open on. |
| `--profiler` or `--profiler=<port>` | Stream live timings for [Phasma Profiler](profiler.md). |
| `--profiler-port <port>` | Port of that stream. The profiler defaults to the same loopback port. |

**Live profiler** on the launcher is the switch that adds `--profiler` and opens the profiler for you.

The player can also expose an opt-in loopback MCP server (Lua, screenshots, state, and tools a script registers) so a shipped game can be driven from outside. That server is off until the game or the launch arguments turn it on. The editor's MCP server is a different process, on port 8765; do not confuse the two.

Play mode inside the editor is not the Player. Editor play snapshots the scene, runs scripts, and restores the scene when you press Stop. The Player is the standalone window.

## Export

`PhasmaExport` packs a project into a folder you can ship. It is a console program:

```text
PhasmaExport --project <project-dir> --output <export-dir> [--force]
```

| Argument | What it does |
|---|---|
| `--project`, `-p` | Project root (the folder that contains `phasma_project.json`). |
| `--output`, `-o` | Destination directory. |
| `--force` | Overwrite an export that is already there. |
| `--help`, `-h` | Print the usage line. |

## Game driver

`PhasmaGameDriver` is a separate Windows console program. It attaches to a Player that is already serving MCP, asks the game for legal actions, and sends back an action id. It does not contain a game's rules.

The driver's own write-up, including the Prism Tetris race launcher and the Jev key commands, is [Phasma/GameDriver/README.md](../../Phasma/GameDriver/README.md). The flags a person actually passes:

| Flag | What it does |
|---|---|
| `--url` | Player MCP endpoint. Default `http://127.0.0.1:8876/mcp`. |
| `--seed` | Reset the game with this seed. Omit it to attach without resetting. |
| `--pace-ms N` | Each decision takes at least N milliseconds (0–10000, default 0). |
| `--key-source vault` | Read the saved Jev key from Windows Credential Manager instead of `JEV_API_KEY` / `TYPESAFE_API_KEY`. |
| `--key-status` | Print whether a key is saved. Does not connect to a game. |
| `--save-key-from-clipboard` | Store the clipboard text as the key. Does not connect. |
| `--remove-key` | Remove the saved key. Does not connect. |

The policy is required. `local` is an offline numeric baseline. `jev` calls the model and needs a key in the environment, or the vault flag. Do not put the key in a file you commit.
