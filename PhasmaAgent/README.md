# PhasmaAgent

Standalone, provider-agnostic AI agent library for C++20.
No engine, ImGui, Vulkan, or SDL dependencies. Drop it into any project.

## Requirements

- C++20 compiler (MSVC 2022, GCC 12+, Clang 15+)
- CMake 3.20+
- One of:
  - **Anthropic API key** — from `console.anthropic.com`, requires OpenSSL-enabled build for HTTPS
  - **OpenAI API key** — from `platform.openai.com`
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
config.model        = "qwen2.5:7b";
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

## Environment Variables

| Variable            | Description                                             | Default             |
|---------------------|---------------------------------------------------------|---------------------|
| `PAGENT_API_KEY`    | Anthropic API key (required for `Provider::Anthropic`)  | —                   |
| `PAGENT_OLLAMA_URL` | Ollama base URL                                         | `http://localhost:11434` |
| `PAGENT_MODEL`      | Model name override                                     | provider default    |

These are read at `Agent` construction time if the corresponding `AgentConfig` fields are empty.

## Authentication

A summary of what works and what does not:

| Method | Works | Notes |
|---|---|---|
| Anthropic API key | Yes | From `console.anthropic.com` — pay-per-token, free credits on signup |
| OpenAI API key | Yes | From `platform.openai.com` — pay-per-token |
| Ollama (local) | Yes | Free, no key — plain HTTP, runs on your machine |
| LM Studio (local) | Yes | Free, no key — OpenAI-compatible local server |
| Claude.ai Pro/Max subscription | No (directly) | Subscriptions are for the web UI and Anthropic's own tools only |
| Claude Code CLI auth | No | Claude Code is not a server; its OAuth tokens are internal and not reusable |
| Subscription proxy (OpenClaude, etc.) | Unofficial | Third-party proxies wrap claude.ai — fragile, may violate ToS, can break on updates |

## Providers

### Anthropic API key

The most capable option. Sign up at `console.anthropic.com` — you get free credits on a new account, enough for extensive testing. Costs fractions of a cent per typical session.

```cpp
config.provider = pagent::Provider::Anthropic;
config.api_key  = "sk-ant-...";    // or set PAGENT_API_KEY env var
config.model    = "claude-sonnet-4-6";
```

Requires HTTPS. Build httplib with OpenSSL:

```cmake
set(CPPHTTPLIB_OPENSSL_SUPPORT ON)
```

OpenSSL must be installed and findable by CMake (`OPENSSL_ROOT_DIR` if needed).

### Ollama (local, plain HTTP, no key)

Free, runs entirely on your machine. Best choice for development without any account setup.

```bash
ollama serve          # start the server
ollama pull qwen2.5:7b
```

```cpp
config.provider = pagent::Provider::OpenAI;   // Ollama speaks OpenAI-compatible API
config.base_url = "http://localhost:11434";
config.model    = "qwen2.5:7b";
```

### OpenAI / LM Studio / any OpenAI-compatible endpoint

```cpp
config.provider = pagent::Provider::OpenAI;
config.api_key  = "sk-...";
config.model    = "gpt-4o";
// config.base_url = "http://localhost:1234";  // LM Studio (no key needed)
```

### Subscription proxies (OpenClaude, etc.)

Third-party projects that expose a local OpenAI-compatible endpoint backed by your Claude.ai subscription cookie. PhasmaAgent can connect to them, but they are unofficial, can break when Anthropic updates their web UI, and may violate the Claude.ai Terms of Service.

```cpp
config.provider = pagent::Provider::OpenAI;   // proxy speaks OpenAI-compatible API
config.base_url = "http://localhost:8080";     // wherever the proxy listens
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

## PhasmaEditor Tools

When integrated inside PhasmaEditor the following tools are registered:

| Tool | Description |
|---|---|
| `get_scene_info` | Lists all entities and models currently loaded in the scene |
| `get_metrics` | Returns current FPS, CPU frame time, and GPU time in ms |
| `compile_shaders` | Triggers a full shader recompile (same as the editor button) |
| `get_render_settings` | Returns current rendering toggles (bloom, TAA, SSAO, SSR, DOF, etc.) |
| `set_render_setting` | Enables or disables a named render setting |
| `get_camera_info` | Returns camera position, yaw, pitch, FOV, near/far planes |
| `set_camera_position` | Moves the camera to a world-space position |
| `load_model` | Loads a 3D model from an absolute filesystem path |
| `list_directory` | Lists files and subdirectories at a given path |

## Limitations

- **HTTPS required for Anthropic API** — cpp-httplib needs to be built with OpenSSL when calling `api.anthropic.com` directly. Ollama and local subscription proxies (OpenClaude, etc.) work with plain HTTP and need no API key.
- **Small models are unreliable for tool use** — `llama3.2:3b` frequently ignores tool schemas or emits JSON code blocks instead of tool calls. Recommended minimum: `qwen2.5:7b` or `llama3.1:8b`.
- **Tool handlers run on a worker thread** — any engine state mutation must be deferred to the main thread (see deferred queue pattern above).
- **No streaming progress on tool results** — while the model streams text, tool execution is synchronous; the caller sees a `ToolResult` event only after the handler returns.
- **Agentic loop has a hard cap** — `AgentConfig::max_tool_rounds` (default 10) prevents runaway loops. Raise it for tasks that require many sequential tool calls.
- **No multi-modal input** — image or file attachments are not supported; text only.
- **No persistent memory** — conversation history is in-process only; it resets when the agent is destroyed or `ClearHistory()` is called.
