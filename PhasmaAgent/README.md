# PhasmaAgent

Standalone, provider-agnostic AI agent library for C++20.
No engine, ImGui, Vulkan, or SDL dependencies. Drop it into any project.

## Requirements

- C++20 compiler (MSVC 2022, GCC 12+, Clang 15+)
- CMake 3.20+
- One of:
  - **Anthropic API key** — from `console.anthropic.com`, requires OpenSSL-enabled build for HTTPS
  - **OpenAI API key** — from `platform.openai.com`
  - **Google Gemini API key** — from `aistudio.google.com`, uses OpenAI-compatible endpoint
  - **Ollama** — running locally (`ollama serve`), no key, no HTTPS
  - **LM Studio** — running locally, no key, no HTTPS

Third-party headers are bundled:
- `third_party/httplib/httplib.h` — cpp-httplib (single header)
- `third_party/nlohmann/json.hpp` — nlohmann/json (single header)

## CMake Integration

```cmake
add_subdirectory(PhasmaAgent)
target_link_libraries(MyApp PRIVATE PhasmaAgent)
```

Include the public header:

```cpp
#include "PhasmaAgent/Agent.h"
#include "PhasmaAgent/AgentUtils.h"  // optional helpers for tool implementors
```

## Quick Start

```cpp
pagent::AgentConfig config;
config.provider     = pagent::Provider::OpenAI;   // Ollama uses OpenAI-compatible API
config.base_url     = "http://localhost:11434";    // Ollama default
config.model        = "qwen3:14b";
config.system_prompt = "You are a helpful assistant.";
config.max_tool_rounds = 10;

pagent::Agent agent(config);

agent.SetEventCallback([](const pagent::AgentEvent &ev) {
    if (ev.type == pagent::AgentEventType::TextDelta)
        std::cout << ev.text;
    else if (ev.type == pagent::AgentEventType::TurnComplete)
        std::cout << "\n[done]\n";
    else if (ev.type == pagent::AgentEventType::Error)
        std::cerr << "Error: " << ev.error_message << "\n";
});

agent.Send("Hello!");

// Call each frame / in your main loop:
while (agent.IsBusy())
    agent.Poll();
```

## Environment Variables (PhasmaEditor)

PhasmaEditor reads these env vars at startup to configure the Agent widget. Set them before launching the editor.

| Variable                  | Description                                                        | Default               |
|---------------------------|--------------------------------------------------------------------|-----------------------|
| `PAGENT_ANTHROPIC_API_KEY`| API key for Anthropic (from `console.anthropic.com`)               | —                     |
| `PAGENT_OPENAI_API_KEY`   | API key for OpenAI (from `platform.openai.com`)                    | —                     |
| `PAGENT_GEMINI_API_KEY`   | API key for Google Gemini (from `aistudio.google.com`)             | —                     |
| `PAGENT_PROVIDER`         | Which provider to select by default: `anthropic`, `openai`, `gemini`, `ollama`. | first available |

### Provider discovery logic

All providers with a valid API key are discovered and available in the editor's provider dropdown. Ollama is always included (no key needed).

1. If `PAGENT_ANTHROPIC_API_KEY` is set → Anthropic added (default model: `claude-haiku-4-5`)
2. If `PAGENT_OPENAI_API_KEY` is set → OpenAI added (default model: `gpt-4.1-mini`)
3. If `PAGENT_GEMINI_API_KEY` is set → Gemini added (default model: `gemini-2.5-flash`)
4. Ollama is always added (default model: `llama3.2`, no key required)

`PAGENT_PROVIDER` selects which provider is active by default. If not set, the first discovered provider is used.

### Examples

**Ollama (local, no key needed):**
```bash
export PAGENT_PROVIDER="ollama"
# Ollama is always available, default model: llama3.2
# Ollama runs automatically when needed (can also be stopped manually to remove GPU memory stress) 
```

**Anthropic:**
```bash
export PAGENT_ANTHROPIC_API_KEY="sk-ant-..."
export PAGENT_PROVIDER="anthropic"
# default model: claude-haiku-4-5
```

**OpenAI:**
```bash
export PAGENT_OPENAI_API_KEY="sk-..."
export PAGENT_PROVIDER="openai"
# default model: gpt-4.1-mini
```

**Gemini:**
```bash
export PAGENT_GEMINI_API_KEY="AIza..."
export PAGENT_PROVIDER="gemini"
# default model: gemini-2.5-flash
```

**Multiple providers (switch in editor UI):**
```bash
export PAGENT_ANTHROPIC_API_KEY="sk-ant-..."
export PAGENT_OPENAI_API_KEY="sk-..."
export PAGENT_GEMINI_API_KEY="AIza..."
export PAGENT_PROVIDER="anthropic"   # default selection, all available in dropdown
```

## Authentication

| Method | Works | Notes |
|---|---|---|
| Anthropic API key | Yes | From `console.anthropic.com` — pay-per-token, free credits on signup |
| OpenAI API key | Yes | From `platform.openai.com` — pay-per-token |
| Google Gemini API key | Yes | From `aistudio.google.com` — has a free tier |
| Ollama (local) | Yes | Free, no key — plain HTTP, runs on your machine |
| LM Studio (local) | Yes | Free, no key — OpenAI-compatible local server |
| Claude.ai Pro/Max subscription | No (directly) | Subscriptions are for the web UI and Anthropic's own tools only |
| Claude Code CLI auth | No | Claude Code is not a server; its OAuth tokens are internal and not reusable |
| Subscription proxy (OpenClaude, etc.) | Unofficial | Third-party proxies wrap claude.ai — fragile, may violate ToS, can break on updates |

## Providers

### Anthropic

The most capable option for reasoning and code tasks. Sign up at `console.anthropic.com`.

```cpp
config.provider = pagent::Provider::Anthropic;
config.api_key  = "sk-ant-...";
config.model    = "claude-sonnet-4-6";   // or claude-opus-4-6, claude-haiku-4-5, etc.
```

Requires HTTPS. Build httplib with OpenSSL:

```cmake
set(CPPHTTPLIB_OPENSSL_SUPPORT ON)
```

### OpenAI

```cpp
config.provider = pagent::Provider::OpenAI;
config.api_key  = "sk-...";
config.model    = "gpt-4o";   // or o3, gpt-4o-mini, etc.
```

### Gemini

Uses Google's OpenAI-compatible endpoint. Get a key at `aistudio.google.com` (free tier available).

```cpp
config.provider  = pagent::Provider::OpenAI;   // Gemini speaks OpenAI-compatible API
config.base_url  = "https://generativelanguage.googleapis.com/v1beta/openai";
config.api_key   = "AIza...";
config.model     = "gemini-2.0-flash";   // or gemini-2.5-pro, gemini-1.5-flash, etc.
```

### Ollama (local, plain HTTP, no key)

Free, runs entirely on your machine. Best choice for development without any account setup.

```bash
ollama serve
ollama pull qwen3:14b
```

```cpp
config.provider = pagent::Provider::OpenAI;   // Ollama speaks OpenAI-compatible API
config.base_url = "http://localhost:11434";
config.model    = "qwen3:14b";
```

Recommended models for reliable tool use: `qwen3:14b`, `qwen2.5:7b`, `llama3.1:8b`.
Small models (3b and below) frequently ignore tool schemas.

### LM Studio / any OpenAI-compatible endpoint

```cpp
config.provider  = pagent::Provider::OpenAI;
config.base_url  = "http://localhost:1234";   // LM Studio default (no key needed)
config.model     = "your-model-name";
```

### Subscription proxies (OpenClaude, etc.)

Third-party projects that expose a local OpenAI-compatible endpoint backed by your Claude.ai subscription cookie. PhasmaAgent can connect to them, but they are unofficial, can break when Anthropic updates their web UI, and may violate the Claude.ai Terms of Service.

```cpp
config.provider = pagent::Provider::OpenAI;
config.base_url = "http://localhost:8080";
config.model    = "claude-sonnet-4-6";
// no api_key needed — the proxy handles auth with your subscription
```

## Registering Tools

```cpp
pagent::ToolDefinition tool;
tool.name        = "get_time";
tool.description = "Returns the current UTC time as an ISO 8601 string.";
// no properties needed for this tool

tool.handler = [](const std::string & /*args*/) -> std::string {
    return "{\"time\":\"2025-01-01T00:00:00Z\"}";
};

agent.RegisterTool(std::move(tool));
```

Tool handlers **run on a worker thread**. Use `AgentUtils.h` helpers to parse arguments:

```cpp
#include "PhasmaAgent/AgentUtils.h"

tool.handler = [](const std::string &args) -> std::string {
    std::string path = pagent::JsonUnescape(pagent::ExtractArgStr(args, "path"));
    float scale      = pagent::ExtractArgNum(args, "scale");
    // ...
    return pagent::JsonObj({{"status", pagent::JsonStr("ok")}});
};
```

For operations that must run on the main thread, use a deferred queue pattern:

```cpp
// In your host class:
std::mutex m_actionMutex;
std::vector<std::function<void()>> m_pendingActions;

void QueueAction(std::function<void()> fn) {
    std::lock_guard lock(m_actionMutex);
    m_pendingActions.push_back(std::move(fn));
}

void FlushActions() {            // call from main thread each frame
    std::vector<std::function<void()>> local;
    { std::lock_guard lock(m_actionMutex); local.swap(m_pendingActions); }
    for (auto &fn : local) fn();
}

// Inside a tool handler (worker thread):
tool.handler = [this](const std::string &args) -> std::string {
    QueueAction([this] { /* safe engine write */ });
    return "{\"status\":\"queued\"}";
};
```

## AgentUtils.h Reference

| Function | Description |
|---|---|
| `JsonStr(s)` | JSON-encode a string value → `"..."` |
| `JsonUnescape(s)` | Decode JSON string escapes (`\\`, `\"`, `\n`, `\t`, `\r`, `\/`) |
| `JsonObj({pairs})` | Build a flat JSON object from key/value pairs (values pre-encoded) |
| `ExtractArgStr(args, key)` | Extract a string value from a flat JSON args object |
| `ExtractArgNum(args, key)` | Extract a float value from a flat JSON args object |

## PhasmaEditor Tool Examples

Integrated inside PhasmaEditor the following tools are registered:

### Scene & Models

| Tool | Description |
|---|---|
| `get_scene_info` | Lists all entities and models currently loaded in the scene |
| `get_metrics` | Returns current FPS and frame delta time in ms |
| `list_models` | Lists available 3D model assets under Assets/Objects/ (with optional filter) |
| `load_model` | Loads a 3D model and adds it to the scene with an optional unique label |
| `remove_model` | Removes a loaded model by filename/label substring |
| `get_model_info` | Returns node count, mesh count, bounding box, and position for a model |
| `clone_model` | Duplicates a loaded model, optionally placing it at a new position |
| `scatter_models` | Scatters multiple copies of a model with randomized rotation and scale |
| `set_model_transform` | Sets position, rotation (Euler degrees), and per-axis scale for a model |
| `create_primitive` | Creates cube/sphere/plane/cylinder/cone with full transform and unique label |
| `save_scene` | Saves the current scene to a .pescene file |
| `load_scene` | Loads a scene from a .pescene file |

### Camera

| Tool | Description |
|---|---|
| `get_camera_info` | Returns camera position, pitch/yaw, and horizontal FOV |
| `set_camera_position` | Moves the camera to a world-space position |
| `set_camera_rotation` | Sets camera pitch and yaw in degrees |
| `set_camera_fov` | Sets horizontal field of view in degrees |
| `focus_camera_on_model` | Frames a model by moving the camera to face its bounding box |
| `set_camera_dof_focus` | Sets depth of field focus distance to match a target model |

### Rendering

| Tool | Description |
|---|---|
| `get_render_settings` | Returns current rendering toggles (bloom, TAA, SSAO, SSR, DOF, etc.) |
| `set_render_setting` | Enables or disables a named render setting |
| `set_render_value` | Sets a numeric render value (bloom_strength, IBL_intensity, etc.) |
| `set_render_mode` | Switches render pipeline: raster, hybrid, or raytracing |
| `set_time_of_day` | Switches between day and night skybox/IBL |
| `set_skybox_hdr` | Loads a new HDR image as the day or night skybox |
| `compile_shaders` | Triggers a full shader recompile |

### Lighting

| Tool | Description |
|---|---|
| `get_lights` | Returns all lights in the scene (directional, point, spot, area) |
| `add_light` | Adds a light with type, color, intensity, position, and type-specific params |
| `set_light` | Modifies an existing light by type and index |
| `remove_light` | Removes a light by type and index |

### Materials

| Tool | Description |
|---|---|
| `get_materials` | Returns material properties for all meshes of a model |
| `set_material_property` | Sets base color, emissive, metallic, roughness, or alpha cutoff per mesh |

### Shaders

| Tool | Description |
|---|---|
| `list_shaders` | Lists all HLSL shader files in Assets/Shaders/ |
| `read_shader` | Reads shader source code |
| `edit_shader` | Find/replace edit on an existing shader (auto-triggers recompilation) |
| `write_shader` | Creates a new shader file (auto-registers for hot-reload) |

### Filesystem

| Tool | Description |
|---|---|
| `find_file` | Recursively searches for files by name substring under Assets/ |
| `list_directory` | Lists files and subdirectories at a given path |

### Agent Workspace (Persistent Memory)

| Tool | Description |
|---|---|
| `read_file` | Reads a text file from the agent workspace (`Assets/Agent/`) |
| `write_file` | Writes or appends to a text file in the agent workspace |

The agent workspace at `Assets/Agent/` provides persistent storage across editor restarts and model switches:

- **`START.md`** — loaded automatically on every agent startup (via `InjectSystemMessage`). Contains instructions the agent follows at the start of each conversation.
- **`MEMORY.md`** — agent-maintained notes about what it has learned (user preferences, project specifics).
- **`TASKS.md`** — pending task list the agent can update and resume across sessions.
- **`PROGRESSION.md`** — progress log for multi-step work.

The agent reads `START.md` on initialization. The default instructions tell it to check MEMORY.md and TASKS.md at the start of each conversation to resume previous work.

## Limitations

- **HTTPS required for Anthropic and Gemini APIs** — cpp-httplib needs to be built with OpenSSL. Ollama and LM Studio work with plain HTTP and need no API key.
- **Small models are unreliable for tool use** — models under 7b parameters frequently ignore tool schemas or emit JSON code blocks instead of tool calls. Recommended minimum: `qwen3:14b`, `qwen2.5:7b`, or `llama3.1:8b`.
- **Tool handlers run on a worker thread** — any engine state mutation must be deferred to the main thread (see deferred queue pattern above).
- **No streaming progress on tool results** — while the model streams text, tool execution is synchronous; the caller sees a `ToolResult` event only after the handler returns.
- **Agentic loop has a hard cap** — `AgentConfig::max_tool_rounds` (default 20 in PhasmaEditor) prevents runaway loops. Raise it for tasks that require many sequential tool calls.
- **No multi-modal input** — image or file attachments are not supported; text only.
