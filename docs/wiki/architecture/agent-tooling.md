# Agent Tooling

This page is the synthesis layer for the engine's agent-facing surfaces. Code
remains the source of truth.

## Current Layers

- `PhasmaMCP` is the reusable C++ MCP server library. It owns JSON-RPC dispatch,
  HTTP transport, tool schemas, result envelopes, and optional BM25 codebase
  indexing. It is not an LLM client and does not run an agent loop.
- `PhasmaEditor` hosts the in-process MCP server at
  `http://127.0.0.1:8765/mcp`, toggled from `Connection -> MCP Server`.
  Editor tools live in `PhasmaEditor/Code/GUI/Agent/EditorToolCatalog.cpp`.
- `tools/phasma_adk_agent/` is the external ADK sidecar scaffold. Phase 1 uses
  ADK's MCP toolset over Streamable HTTP and filters the editor MCP surface down
  to `query_scene`, `take_screenshot`, and `get_console_log`.

As of 2026-05-19 on `master`, the repository does not contain a tracked
`PhasmaAgent/` directory or CMake target. If an embedded PhasmaAgent lane is
reintroduced, keep in-editor chat, provider routing, tool loops, and retrieval
there. The ADK sidecar should remain the external orchestration lane rather than
reimplementing embedded agent behavior.

## ADK And A2A Direction

The ADK sidecar should connect directly to the editor's Streamable HTTP MCP
endpoint. The `npx mcp-remote` bridge is for stdio-only clients, not for this
sidecar. The Phase 1 probe follows the HTTP lifecycle enough for smoke testing:
`initialize`, `notifications/initialized`, `tools/list`, then optional read-only
`tools/call`, while preserving any `Mcp-Session-Id` returned by the server.

Future A2A exposure should be goal-oriented: inspect the current scene, capture a
visual state, summarize logs, run an approved smoke workflow. Do not re-export
low-level MCP tools such as `write_project_file`, `patch_project_file`,
`execute_lua`, or `inject_mouse_input` over A2A.

The repository's no-autonomous-commit rule applies to sidecar skills. Future
native build or smoke helpers should wrap existing repo entry points, including
Windows batch setup and `tools/perf_cycle.sh`, instead of reconstructing compiler
environment logic.

## Phase 1 Checks

```bash
python3 tools/phasma_adk_agent/mcp_probe.py
python3 tools/phasma_adk_agent/mcp_probe.py --smoke
```

Use `--screenshot` when the smoke should also call `take_screenshot`.

## Commit Validation Routine

`tools/precommit_validate.py` is the one-command validation harness to run before
asking for a commit. It wraps existing CMake output, editor MCP tools, and the
ADK sidecar rather than replacing them.

```powershell
py -3 tools\precommit_validate.py --profile quick
py -3 tools\precommit_validate.py --profile commit
```

The quick profile is the daily routine: git hygiene, `third_party/` guard,
instruction-file sync, ADK read-only tool-filter guard, `clang-format` dry-run
for changed C/C++ files, Python syntax checks, wiki lint, editor MCP
launch/probe, and ADK smoke when `adk` plus an API key are present. The
validator writes the build output `Assets/Agent/agent_config.json` with
`"mcp": true` before launching PhasmaEditor and passes `--display 1` by default;
use `--display 0` only for the primary monitor. If MCP is already reachable, the
script reuses the running editor.

The commit profile adds the Release editor/player/launcher build,
PhasmaLauncher smoke, MCP `execute_lua` internal round-trip, console error
scans, screenshot capture plus PNG sanity checks, Vulkan/DX12 Sponza editor
smokes, and Vulkan/DX12 PhasmaPlayer smokes. Use `--profile full` to force
validation-layer environment variables and require the ADK smoke. Use
`--perf-baseline` and `--visual-baseline` for optional snapshot and screenshot
parity comparisons. The performance path loads `Scenes/sponza.pescene`,
reapplies immediate present mode, captures 10 snapshots 1 second apart, then
calls `tools/compare_snapshots.py`.

Scene paths have two conventions in this flow: editor startup config stores the
asset-prefixed path, such as `Assets/Scenes/sponza.pescene`, while Lua executes
from the asset root and therefore loads the same scene as `Scenes/sponza.pescene`.

The deeper behavior probes are opt-in rather than part of the default commit
gate: `--play-lifecycle` drives editor start/pause/resume/stop over MCP,
`--script-tests` runs the editor script test action and scans the log, and
`--hot-reload-smoke` reloads the editor module then waits for MCP to return.
