# Phasma MCP Probe

This package retains its historical directory name for existing imports, but it
no longer contains or depends on Google ADK. `mcp_probe.py` is a standard-library
client for PhasmaEditor's loopback MCP endpoint.

Start PhasmaEditor, enable `Connection -> MCP Server`, then run from the
repository root:

```powershell
py -3 tools\phasma_adk_agent\mcp_probe.py
py -3 tools\phasma_adk_agent\mcp_probe.py --smoke
py -3 tools\phasma_adk_agent\mcp_probe.py --smoke --screenshot
```

The client accepts only HTTP loopback URLs. `--smoke` calls `query_scene` and
`get_console_log`; `--screenshot` also calls `take_screenshot`.

The repository validation harness reuses the same client:

```powershell
py -3 tools\precommit_validate.py --profile quick
py -3 tools\precommit_validate.py --profile commit
```
