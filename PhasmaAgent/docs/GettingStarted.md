# Getting Started with PhasmaAgent

## 1. Add to your CMake project

```cmake
add_subdirectory(PhasmaAgent)
target_link_libraries(MyApp PRIVATE PhasmaAgent)
```

For Anthropic or Gemini (HTTPS required):

```cmake
set(CPPHTTPLIB_OPENSSL_SUPPORT ON)
```

## 2. Include

```cpp
#include "PhasmaAgent/Agent.h"
#include "PhasmaAgent/AgentUtils.h"  // tool argument helpers (optional)
```

## 3. Configure and create

```cpp
pagent::AgentConfig config;
config.provider      = pagent::Provider::Anthropic;
config.api_key       = "sk-ant-...";
config.model         = "claude-sonnet-4-6";
config.system_prompt = "You are a helpful assistant.";
config.max_tool_rounds = 10;

pagent::Agent agent(config);
```

### Providers

| Provider | `provider` enum | `base_url` | Key env var |
|---|---|---|---|
| Anthropic | `Provider::Anthropic` | *(none)* | `PAGENT_ANTHROPIC_API_KEY` |
| OpenAI | `Provider::OpenAI` | *(none)* | `PAGENT_OPENAI_API_KEY` |
| Gemini | `Provider::OpenAI` | `https://generativelanguage.googleapis.com/v1beta/openai` | `PAGENT_GEMINI_API_KEY` |
| Ollama (local) | `Provider::OpenAI` | `http://localhost:11434` | *(none)* |
| LM Studio | `Provider::OpenAI` | `http://localhost:1234` | *(none)* |

Auto-discover from environment variables:

```cpp
auto providers = pagent::DiscoverProviders();
int idx        = pagent::GetDefaultProviderIndex(providers);
// providers[idx].provider, .apiKey, .defaultModel
```

Environment variables read: `PAGENT_ANTHROPIC_API_KEY`, `PAGENT_OPENAI_API_KEY`,
`PAGENT_GEMINI_API_KEY`, `PAGENT_PROVIDER` (`anthropic` / `openai` / `gemini` / `ollama`).

## 4. Set the event callback

```cpp
agent.SetEventCallback([](const pagent::AgentEvent &ev) {
    switch (ev.type) {
    case pagent::AgentEventType::TextDelta:
        std::cout << ev.text;
        break;
    case pagent::AgentEventType::TurnComplete:
        std::cout << "\n[done]\n";
        break;
    case pagent::AgentEventType::Error:
        std::cerr << "Error: " << ev.error_message << "\n";
        break;
    default: break;
    }
});
```

Full event type list: `TextDelta`, `TextComplete`, `ThinkingDelta`, `ThinkingComplete`,
`ToolCallBegin`, `ToolCallComplete`, `ToolResult`, `Usage`, `TurnComplete`, `Error`, `Info`.

## 5. Register tools

```cpp
agent.RegisterTool({
    .name        = "read_file",
    .description = "Read a UTF-8 text file from disk.",
    .properties  = {
        {"path", "Absolute or relative file path", pagent::SchemaType::String, true}
    },
    .handler = [](const std::string &args) -> std::string {
        std::string path = pagent::ExtractArgStr(args, "path");
        std::ifstream f(path);
        if (!f) return pagent::JsonObj({{"error", pagent::JsonStr("not found")}});
        std::string content((std::istreambuf_iterator<char>(f)), {});
        return pagent::JsonObj({{"content", pagent::JsonStr(content)}});
    }
});
```

Tool handlers **run on a worker thread**. For main-thread-only operations use the deferred queue pattern:

```cpp
// Host class members:
std::mutex m_actionMutex;
std::vector<std::function<void()>> m_pendingActions;

void QueueAction(std::function<void()> fn) {
    std::lock_guard lock(m_actionMutex);
    m_pendingActions.push_back(std::move(fn));
}
void FlushActions() {  // call from main thread each frame
    std::vector<std::function<void()>> local;
    { std::lock_guard lock(m_actionMutex); local.swap(m_pendingActions); }
    for (auto &fn : local) fn();
}

// Inside a tool handler (worker thread):
tool.handler = [this](const std::string &args) -> std::string {
    std::promise<std::string> p;
    auto fut = p.get_future();
    QueueAction([&p] {
        // safe main-thread write
        p.set_value("{\"status\":\"ok\"}");
    });
    return fut.get();  // blocks worker thread until main thread resolves
};
```

### AgentUtils.h helpers

| Function | Description |
|---|---|
| `ExtractArgStr(args, key)` | Get a string field from the JSON args object |
| `ExtractArgInt(args, key, default)` | Get an integer field |
| `ExtractArgNum(args, key)` | Get a float field |
| `JsonStr(s)` | JSON-encode a string → `"..."` |
| `JsonUnescape(s)` | Decode JSON string escapes |
| `JsonObj({pairs})` | Build a flat JSON object from key/value pairs |
| `SanitizeUTF8(s)` | Strip invalid UTF-8 bytes (safe for JSON serialisation) |
| `IsPathSafe(path, root)` | Reject path traversal attempts |
| `Base64Encode(data, len)` | Base64-encode binary data |

## 6. Send and poll

```cpp
agent.Send("Summarise the file main.cpp");

// In your app/game loop:
while (agent.IsBusy())
    agent.Poll();  // drains event queue, fires your callback
```

`Poll()` is non-blocking and safe to call every frame even when idle.

## 7. Model management (Ollama)

```cpp
// List locally installed models (vision + tools only)
auto models = pagent::Agent::FetchModelInfos(pagent::Provider::Ollama, "", "", /*local_only=*/true);

// Pull a model in the background
auto token = pagent::Agent::PullModel("qwen3.5:cloud",
    [](const std::string &status) { printf("%s\n", status.c_str()); },
    [](bool ok) { printf(ok ? "done\n" : "failed\n"); });

// Cancel the pull
pagent::Agent::CancelPull(token);

// Check capabilities
auto caps = pagent::Agent::QueryCapabilities(pagent::Provider::Ollama, "qwen3.5");
// caps.vision, caps.tools

// Free GPU memory
pagent::Agent::UnloadModel(pagent::Provider::Ollama, "nemotron-3-super:latest");
```

## 8. Session management

```cpp
// Save
auto history = agent.GetHistory();
// serialize history however you like, e.g. to JSON

// Restore
agent.LoadHistory(history);

// Trim when history grows large (summarises old turns via the LLM)
agent.ForceCompact(/*keepRecent=*/4);

// Or set automatic summarisation in config:
config.max_history_messages    = 20;
config.summarize_after_messages = 8;  // summarise when history exceeds 8
```

## 9. Model routing (optional)

Automatically route queries to cheaper or more powerful models based on keyword complexity:

```cpp
config.routing.enabled      = true;
config.routing.simple_model = "gemini-2.5-flash-lite";  // explain / find / Q&A
config.routing.complex_model = "gemini-2.5-pro";        // architecture / multi-file rewrites
// Medium queries (the default) always use config.model
```

## 10. Custom HTTP transport (optional)

Bypass httplib entirely with your own HTTP client:

```cpp
config.custom_http_handler = [](
    const std::string &method,
    const std::string &url,
    const std::map<std::string, std::string> &headers,
    const std::string &body) -> std::pair<int, std::string>
{
    // use libcurl, WinHTTP, etc.
    return {200, response_body};
};
```

## Custom provider backend (optional)

Implement `IProviderBackend` to add any LLM that isn't already supported:

```cpp
config.custom_backend = std::make_shared<MyBackend>();
```

See `IProviderBackend` in `Agent.h` for the six methods to implement.
