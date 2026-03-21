# PhasmaAgent — Instructions

Supplements the root `INSTRUCTIONS.md`. Read that file first.

PhasmaAgent is a **standalone static library** — no Vulkan, no ImGui, no engine dependencies.
It compiles independently and can be used in any C++ project.
Namespace: `pagent`. Headers: `PhasmaAgent/include/PhasmaAgent/`.

**This library does NOT use PhasmaPch.h.** Add explicit `#include` directives to all source files.

---

## Directory Map

```
PhasmaAgent/
├── include/PhasmaAgent/
│   ├── Agent.h             # Public API — Agent, AgentConfig, ToolDefinition, events, providers
│   ├── AgentUtils.h        # Inline helpers: ExtractArgStr/Int/Array, JsonStr/Obj, IsPathSafe, Base64
│   ├── BM25Index.h         # Keyword search index (camelCase + snake_case tokenization)
│   ├── VectorStore.h       # Embedding vector store (cosine similarity)
│   ├── IncludeGraph.h      # C++ include dependency graph
│   ├── ASTChunker.h        # C++ source → logical code chunks for indexing
│   ├── CodebaseIndexer.h   # Indexes source dirs into BM25 + VectorStore
│   ├── RepoMap.h           # Compact codebase overview for system prompt (~1K tokens)
│   ├── OpenAIEmbedding.h   # OpenAI text-embedding-* provider
│   ├── GoogleEmbedding.h   # Gemini embedding provider
│   ├── OllamaEmbedding.h   # Local Ollama embedding provider
│   ├── CodebaseContext.h   # Owns VectorStore + BM25Index + IncludeGraph + async status checks
│   ├── EmbeddingUtils.h    # CreateEmbeddingProvider(kind, model, key) factory
│   ├── RagUtils.h          # Hybrid BM25 + vector retrieval helpers
│   ├── ProviderUtils.h     # Environment variable reading helpers
│   └── OllamaModelUtils.h  # Ollama model pull/unload helpers
└── src/
    ├── Agent.cpp           # Agent::Send, Poll, ForceCompact, provider factory
    ├── AgentImpl.h/.cpp    # Pimpl implementation
    ├── RequestWorker.h/.cpp# Background thread: HTTP request + SSE stream + tool loop
    ├── ConversationHistory.h/.cpp  # History management, trim, compact
    ├── AnthropicBackend.h/.cpp     # Claude API (SSE streaming)
    ├── OpenAIBackend.h/.cpp        # OpenAI / Ollama compatible API
    ├── GoogleBackend.h/.cpp        # Gemini API (SSE streaming)
    ├── StreamParser.h/.cpp         # SSE event parsing
    ├── ToolRegistry.h/.cpp         # Tool registration + dispatch
    ├── EventQueue.h/.cpp           # Thread-safe event queue (worker → main thread)
    ├── BM25Index.cpp               # BM25 + SearchMulti (parallel)
    ├── VectorStore.cpp             # CosineSimilarity + SearchMulti (parallel)
    ├── ASTChunker.cpp              # Heuristic C++ chunking
    ├── CodebaseIndexer.cpp         # Directory walker + indexer
    ├── IncludeGraph.cpp
    ├── RepoMap.cpp
    ├── ModelRouter.cpp             # QueryComplexity classification
    ├── ImageDescriber.cpp          # Vision fallback via Gemini
    ├── OpenAIEmbedding.cpp
    ├── GoogleEmbedding.cpp
    ├── OllamaEmbedding.cpp
    ├── CodebaseContext.cpp
    ├── EmbeddingUtils.cpp
    ├── RagUtils.cpp
    ├── ProviderUtils.cpp
    └── OllamaModelUtils.cpp
```

---

## Public API Overview

### Creating an Agent

```cpp
pagent::AgentConfig config;
config.provider             = pagent::Provider::Anthropic;
config.api_key              = std::getenv("PAGENT_ANTHROPIC_API_KEY");
config.model                = "claude-sonnet-4-6";
config.system_prompt        = "You are a Vulkan engine expert...";
config.max_tokens           = 8192;
config.max_tool_rounds      = 15;
config.max_history_messages = 40;

// Context management
config.summarize_after_messages    = 20;   // auto-compact when history exceeds N
config.summarize_tool_result_chars = 8000; // summarize large tool results
config.max_tool_result_chars       = 4000; // hard truncate beyond this

// Model routing (route cheap queries to cheaper models)
config.routing.enabled      = true;
config.routing.simple_model  = "claude-haiku-4-5";
config.routing.complex_model = "claude-opus-4-6";

// Embedding / RAG
config.embedding_provider = std::make_shared<pagent::GoogleEmbedding>(geminiKey, "text-embedding-004");
config.rag_top_k           = 5;
config.rag_min_score       = 0.15f;
config.rag_max_context_chars = 8000;

pagent::Agent agent(config);
```

### Registering Tools

```cpp
agent.RegisterTool({
    .name        = "read_file",
    .description = "Reads a file and returns its content.",
    .properties  = {
        {"path", "File path", pagent::SchemaType::String, /*required=*/true},
        {"limit", "Max lines to return", pagent::SchemaType::Integer, false},
    },
    .handler = [](const std::string& args) -> std::string {
        // Runs on WORKER THREAD.
        std::string path = pagent::ExtractArgStr(args, "path");
        // ...
        return nlohmann::json{{"content", text}}.dump();
    }
});
```

### Sending Messages and Polling

```cpp
// Non-blocking send
agent.Send("Explain the RenderGraph class");

// Call each frame on main thread to drain event queue
agent.SetEventCallback([](const pagent::AgentEvent& event) {
    switch (event.type) {
        case pagent::AgentEventType::TextDelta:    /* streaming text */ break;
        case pagent::AgentEventType::TextComplete: /* full turn text */ break;
        case pagent::AgentEventType::ToolCallBegin:   /* tool started */ break;
        case pagent::AgentEventType::ToolResult:      /* tool finished */ break;
        case pagent::AgentEventType::TurnComplete:    /* agentic loop done */ break;
        case pagent::AgentEventType::Error:           /* error */ break;
        case pagent::AgentEventType::Usage:           /* token stats */ break;
    }
});
agent.Poll(); // call every frame
```

### History Management

```cpp
agent.ClearHistory();
agent.GetHistory();                     // returns vector<HistoryEntry>
agent.LoadHistory(entries);             // restore a saved session
agent.ForceCompact(/*keepRecent=*/4);   // summarize old messages in-place
agent.InjectSystemMessage("...");       // inject without sending a request
```

### Codebase Indexing

```cpp
auto store = std::make_shared<pagent::VectorStore>();
auto bm25  = std::make_shared<pagent::BM25Index>();

pagent::CodebaseIndexer indexer(embProvider.get(), store.get(), bm25.get(),
    [](int done, int total, const std::string &file) { /* progress */ });

pagent::IndexerConfig cfg;
cfg.directories = {"PhasmaCore/Code", "PhasmaEditor/Code"};
// Index() blocks — call from a background thread
std::thread([&] { indexer.Index(cfg); }).detach();

agent.SetCodebaseStore(store.get());
agent.SetCodebaseBM25(bm25.get());
```

---

## BM25Index

Thread-safe keyword search index with camelCase/snake_case tokenization.

```cpp
bm25->Add(id, content);           // add/replace document
bm25->Remove(id);                  // remove by id
bm25->RemoveByFile(file);          // remove all docs from a file
bm25->Search(query, top_k);        // single query
bm25->SearchMulti(queries, top_k); // parallel multi-query (merges by max score)
bm25->Size();
bm25->Clear();
```

`SearchMulti` launches one `std::async` thread per query. BM25 uses `shared_mutex`
so multiple concurrent reads are safe. Results are merged by max-score per document ID.

---

## VectorStore

Thread-safe embedding store with cosine similarity search.

```cpp
store->Add({.id="f:10-50", .content="...", .metadata="{}", .embedding={...}});
store->Search(queryEmbedding, top_k, min_score);       // single query
store->SearchMulti(embeddings, top_k, min_score);      // parallel (holds one shared_lock)
store->SaveToBinary(path);   // fast binary serialization
store->LoadFromBinary(path); // load with dimension check
store->Size();
store->ForEachEntry(fn);     // iterate all entries under shared lock
```

`SearchMulti` takes one `shared_lock` and fans out N cosine similarity passes in parallel
async threads. Returned `SearchResult::entry` pointers are valid while the lock is held
(caller must process results before any write operations on the store).

---

## AgentUtils.h — Helper Functions

All are inline in the header; no linking required.

```cpp
// Argument extraction from JSON tool-call args
std::string              ExtractArgStr  (args, "key");
int64_t                  ExtractArgInt  (args, "key", defaultVal);
float                    ExtractArgNum  (args, "key");
std::vector<std::string> ExtractArgArray(args, "key");

// JSON construction
std::string JsonStr(s);                          // JSON-encode a string value
std::string JsonUnescape(s);                     // unescape a JSON string value
std::string JsonObj({{"key", JsonStr(val)}});    // build flat JSON object

// Security
bool IsPathSafe(path, allowedRoot);  // rejects path traversal

// Encoding
std::string         Base64Encode(data, len);
std::vector<uint8_t> Base64Decode(str);
std::string         SanitizeUTF8(str);  // strip invalid bytes before JSON serialization
```

---

## Provider Backends

Implement `IProviderBackend` to add a new LLM provider:

```cpp
class MyBackend : public pagent::IProviderBackend
{
public:
    std::string BuildRequestJson(...) const override;
    bool ParseStreamEvent(const std::string& data, std::vector<AgentEvent>& out) override;
    void ResetStreamState() const override;
    std::string BuildToolsJson(const std::vector<ToolDefinition>&) const override;
    NeutralMessage FormatToolResult(...) const override;
    std::string GetEndpointPath() const override;
    std::pair<std::string,std::string> GetAuthHeader(const std::string& key) const override;
    bool SupportsVision() const override { return true; }
};

// Inject via config
config.custom_backend = std::make_shared<MyBackend>();
```

---

## Threading Model

```
Main thread:          agent.Send(msg)  →  enqueues work
                      agent.Poll()     →  drains EventQueue, fires EventCallback

Worker thread:        RequestWorker    →  HTTP request → SSE stream → tool dispatch loop
                      tool.handler()   →  runs on worker thread (MUST be thread-safe)

Background threads:   std::async       →  BM25/vector SearchMulti parallel queries
                      CodebaseIndexer  →  file walking + embedding (separate thread)
```

**Rule**: Tool handlers run on the worker thread. Any main-thread-only operations (ImGui, GPU)
must use the deferred `std::promise` pattern — post work to the main thread and wait for the result.

---

## Model Routing

```cpp
// Automatic complexity classification based on query keywords
pagent::QueryComplexity complexity = pagent::ClassifyQuery(userMessage);
// Simple  → cheap model  (explain, find, list)
// Medium  → default model
// Complex → capable model (architecture, multi-file rewrites)
```

Configure via `AgentConfig::routing`. Empty string in `simple_model`/`complex_model` = use default.

---

## Environment Variables

| Variable | Purpose |
|---|---|
| `PAGENT_ANTHROPIC_API_KEY` | Anthropic / Claude API key |
| `PAGENT_OPENAI_API_KEY` | OpenAI API key |
| `PAGENT_GEMINI_API_KEY` | Google Gemini API key |
| `PAGENT_VOYAGE_API_KEY` | Voyage AI embeddings API key |

These variables are consumed by whatever host application configures `pagent::Agent`.
Provider selection and model choice belong to the host application (PhasmaEditor uses `agent_config.json`).

---

## Rules Specific to PhasmaAgent

- No engine includes (`RHI.h`, `Image.h`, ImGui, SDL, etc.)
- No PhasmaPch.h — add explicit `#include` to every source file that needs them
- Add `<future>` and `<unordered_map>` explicitly when using them
- Tool handlers must be thread-safe
- All JSON serialization must use `json::error_handler_t::replace` in `.dump()` calls
  to avoid crashes on invalid UTF-8 in tool results
