# Phasma ADK Agent

Read-only Phase 1 ADK sidecar for a running PhasmaEditor instance. It connects
to the editor's existing MCP endpoint at `http://127.0.0.1:8765/mcp` and exposes
only the ADK-visible inspection tools needed for the first wedge:

- `tools/list` through the MCP protocol probe
- `query_scene`
- `take_screenshot`
- `get_console_log`

No native build/test tools are included yet.

## Boundary

- `PhasmaMCP` is the engine-independent C++ MCP server library.
- `PhasmaEditor` hosts the in-process MCP server and owns the editor tool catalog.
- This ADK sidecar is an external orchestration client over Streamable HTTP.

Current `master` does not contain a tracked `PhasmaAgent/` directory or CMake
target. If an embedded PhasmaAgent lane is reintroduced, keep provider routing,
tool-call loops, and in-editor chat there. Keep this sidecar focused on external
orchestration and, later, a goal-oriented A2A facade.

Do not re-export low-level MCP tools over A2A. Tools such as
`write_project_file`, `patch_project_file`, `execute_lua`, and
`inject_mouse_input` stay MCP-internal and are filtered out of `agent.py`.

The no-autonomous-commit rule applies here too. Future build/smoke helpers
should wrap the repository's existing `.bat` files and `tools/perf_cycle.sh`
instead of recreating Windows compiler environment setup.

## Setup

```bash
cd tools
python3 -m venv .venv
source .venv/bin/activate
pip install -r phasma_adk_agent/requirements.txt
```

PowerShell:

```powershell
cd tools
py -3 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r phasma_adk_agent\requirements.txt
```

The default model is `openai/gpt-4o-mini`, routed through LiteLLM. Set the
provider's API key in the environment before running:

- OpenAI (default): `OPENAI_API_KEY`
- Anthropic: `ANTHROPIC_API_KEY` with `PHASMA_ADK_MODEL=anthropic/claude-3-5-sonnet-latest`
- Gemini via LiteLLM: `GEMINI_API_KEY` with `PHASMA_ADK_MODEL=gemini/gemini-2.0-flash`
- Native Gemini (no LiteLLM): set `PHASMA_ADK_MODEL=gemini-flash-latest` (no `/`) and `GOOGLE_API_KEY`

Any `PHASMA_ADK_MODEL` value containing `/` is treated as a LiteLLM model string
and wrapped in `LiteLlm(...)`. A bare model name is passed through to ADK as-is
(Gemini path).

Start PhasmaEditor and enable `Connection -> MCP Server`.

## Probe MCP

The probe uses only Python's standard library, so it can run before ADK is
installed.

```bash
# From the repository root:
python3 tools/phasma_adk_agent/mcp_probe.py
python3 tools/phasma_adk_agent/mcp_probe.py --smoke
python3 tools/phasma_adk_agent/mcp_probe.py --smoke --screenshot
```

PowerShell:

```powershell
py -3 tools\phasma_adk_agent\mcp_probe.py
py -3 tools\phasma_adk_agent\mcp_probe.py --smoke
```

`--smoke` calls `query_scene` and `get_console_log`. `--screenshot` also calls
`take_screenshot`, which writes the editor's screenshot artifact.

## Run ADK

Run ADK from the parent directory so it can discover the `phasma_adk_agent`
package.

```bash
cd tools
source .venv/bin/activate
adk run phasma_adk_agent
```

For the ADK dev web UI:

```bash
cd tools
source .venv/bin/activate
adk web --port 8000
```

Set `PHASMA_ADK_MCP_URL` to point at a different MCP endpoint, and
`PHASMA_ADK_MODEL` to override the model passed to ADK.

## Pre-Commit Validation

The ADK sidecar can be included in a broader validation routine through the
repo-level script:

```powershell
cd <repo-root>
.\tools\.venv\Scripts\Activate.ps1
$env:OPENAI_API_KEY="..."
$env:PHASMA_ADK_MODEL="openai/gpt-4o-mini"
py -3 tools\precommit_validate.py --profile quick
py -3 tools\precommit_validate.py --profile commit
```

`--profile quick` is the daily routine check: git hygiene, `third_party/`
guard, instruction-file sync, ADK read-only tool-filter guard, `clang-format`
dry-run for changed C/C++ files, Python syntax, wiki lint, auto-launch or reuse
an editor MCP session, probe MCP, and run an ADK smoke prompt when `adk` plus an
API key are available. The validator finds `adk` on PATH or in `tools/.venv`,
and reads ignored local `.env` files for model keys. It writes the build output
`Assets/Agent/agent_config.json` with `"mcp": true` before launching the editor
and passes `--display 1` by default. Use `--display 0` only when you want the
primary monitor.

`--profile commit` adds the Release build, PhasmaLauncher smoke, MCP
`execute_lua` internal round-trip, console error scans, screenshot capture plus
PNG sanity checks, Vulkan/DX12 Sponza editor smokes, and Vulkan/DX12
PhasmaPlayer smokes. Use `--profile full` to force validation environment
variables and require the ADK smoke. Add `--perf-baseline <dir>` or
`--visual-baseline <png-or-dir>` when you want profiler or screenshot comparison
against a fixed baseline. Performance comparison loads `Scenes/sponza.pescene`,
reapplies immediate present mode, then captures 10 snapshots 1 second apart
before calling `tools/compare_snapshots.py`.

Path convention note: `tools/precommit_validate.py` keeps paired constants for
the default scene path. Editor startup config stores the asset-prefixed form,
`Assets/Scenes/sponza.pescene`, while Lua runs from the asset root and loads the
same scene as `Scenes/sponza.pescene`.

The heavier editor-behavior checks are opt-in until they are boring on every
machine:

```powershell
py -3 tools\precommit_validate.py --profile full --play-lifecycle
py -3 tools\precommit_validate.py --profile full --script-tests
py -3 tools\precommit_validate.py --profile full --hot-reload-smoke
```

`--play-lifecycle` drives start, pause, resume, stop, and a final scene query
through MCP with a longer timeout because restoring a Sponza snapshot can take
noticeably longer than a normal tool call.

Useful overrides:

```powershell
py -3 tools\precommit_validate.py --profile quick --display 1
py -3 tools\precommit_validate.py --profile commit --no-player-smoke
py -3 tools\precommit_validate.py --profile commit --no-launch-editor
```
