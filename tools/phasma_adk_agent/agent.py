"""Read-only ADK sidecar for a running PhasmaEditor MCP server."""

from __future__ import annotations

import os

MCP_URL = os.environ.get("PHASMA_ADK_MCP_URL", "http://127.0.0.1:8765/mcp")
MODEL = os.environ.get("PHASMA_ADK_MODEL", "openai/gpt-4o-mini")

READ_ONLY_MCP_TOOLS = [
    "query_scene",
    "take_screenshot",
    "get_console_log",
]

try:
    from google.adk.agents import Agent
    from google.adk.models.lite_llm import LiteLlm
    from google.adk.tools.mcp_tool import McpToolset

    try:
        from google.adk.tools.mcp_tool import StreamableHTTPConnectionParams
    except ImportError:
        from google.adk.tools.mcp_tool.mcp_session_manager import StreamableHTTPConnectionParams
except ModuleNotFoundError as exc:
    raise RuntimeError(
        "tools/phasma_adk_agent requires google-adk (with litellm). "
        "Install the pinned sidecar dependencies with "
        "`pip install -r tools/phasma_adk_agent/requirements.txt`."
    ) from exc


def _resolve_model(spec: str):
    if "/" in spec:
        return LiteLlm(model=spec)
    return spec


root_agent = Agent(
    model=_resolve_model(MODEL),
    name="phasma_adk_sidecar",
    description="External read-only ADK orchestrator for PhasmaEditor through MCP.",
    instruction=f"""
You are the external ADK sidecar for PhasmaEditor.

Connect to the editor through its MCP server at {MCP_URL}. This Phase 1 agent is
read-only and may use only these MCP tools: query_scene, take_screenshot, and
get_console_log. Use them to inspect scene state, capture visual evidence, and
read recent logs.

Never claim that you queried the scene, read logs, or took a screenshot unless a
tool call returned concrete data in this turn. If the MCP tools are unavailable,
the tool call fails, or the result is empty, say that clearly and give the user
the MCP probe command to run. Do not fill in missing scene details from memory or
generic expectations.

When answering scene/log questions, include concrete fields from the tool result:
scene name or path when present, entity/object counts, cameras, lights, meshes,
and recent warnings/errors. If a field is absent, say it was not present in the
tool result.

Do not call or suggest MCP mutation tools such as write_project_file,
patch_project_file, execute_lua, inject_mouse_input, or project build commands.
Do not commit changes. If a user asks for implementation work, explain that this
sidecar can gather editor context only and hand off the goal to a normal repo
agent or future approved workflow.

When an A2A surface is added later, expose goal-oriented capabilities only. Do
not re-export low-level MCP tools over A2A.
""".strip(),
    tools=[
        McpToolset(
            connection_params=StreamableHTTPConnectionParams(url=MCP_URL),
            tool_filter=READ_ONLY_MCP_TOOLS,
        ),
    ],
)
