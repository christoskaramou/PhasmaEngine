# PhasmaEngine — Instructions

This file is the primary knowledge document for AI coding agents (Codex, Claude Code, Gemini, Copilot, etc.).
Subdirectory `INSTRUCTIONS.md` files add layer-specific detail that cascades on top of this root file.

**Always read `AI_CHANGELOG.md` before starting work** — it tracks every recent AI-assisted change
and is the fastest way to understand current state without digging through git history.

---

## Build Commands

```bash
# Configure (Windows — Visual Studio 2022)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Configure (Linux — Ninja)
cmake -S . -B build -G Ninja

# Build
cmake --build build --config Debug          # Debug
cmake --build build --config Release        # Release
cmake --build build --config RelWithDebInfo # RelWithDebInfo

# Run — must run from build/<config>/ so relative asset paths resolve
./build/Debug/PhasmaEditor.exe
```

Assets and required DLLs (`dxcompiler.dll`, `shaderc_shared.dll`, `SDL2.dll`) are copied
post-build automatically via a CMake custom target (`PhasmaEditorAssets`).
The generated `build/PhasmaEditor.sln` works for Visual Studio IDE workflows.
There is no automated test suite — testing is manual via the editor.

---

## Repository Layout

```
PhasmaEngine/
├── PhasmaCore/               # Static library — Vulkan/SDL2 abstraction, ECS, base utilities
│   ├── Code/
│   │   ├── API/              # RHI, Buffer, Image, Sampler, Command, Queue, Vertex, Pipeline...
│   │   ├── ECS/              # ISystem, IComponent, Context, EventSystem, OrderedMap
│   │   └── Base/             # Path, Log, FileWatcher, utility headers
│   ├── pch/
│   │   └── PhasmaPch.h       # Precompiled header — included in every .cpp via CMake PCH
│   └── CMakeLists.txt
├── PhasmaEditor/             # Executable — editor app (Windows/Linux)
│   ├── Code/
│   │   ├── App/              # App.cpp — boot sequence and main loop
│   │   ├── Systems/          # RendererSystem, LightSystem, CameraSystem, ScriptManager...
│   │   ├── RenderPasses/     # GBuffer, Depth, Shadow, Light, Bloom, TAA, SSAO, SSR, DOF...
│   │   ├── Scene/            # Scene, Model, Geometry, PhysicsHelper
│   │   ├── GUI/              # ImGui wrapper, editor widgets, MCP/tool bridge
│   │   │   └── Agent/        # EditorToolServer (MCP HTTP), EditorToolRuntime, EditorToolCatalog
│   │   └── Script/           # Lua binding layer
│   └── Assets/
│       ├── Shaders/          # HLSL sources compiled to SPIR-V
│       ├── Objects/          # 3D models (.glb, .gltf, .fbx, .obj)
│       └── Agent/            # In-engine agent workspace (MEMORY.md, TASKS.md, Lua scripts)
├── PhasmaAgent/              # Standalone AI agent library (no engine/Vulkan/ImGui deps)
│   ├── include/PhasmaAgent/  # Public headers: Agent.h, BM25Index.h, VectorStore.h...
│   └── src/                  # Provider backends, RequestWorker, indexing, embeddings
└── AI_CHANGELOG.md           # ← READ THIS FIRST — running log of AI-assisted changes
```

---

## Architecture

### CMake Targets

| Target | Type | Description |
|---|---|---|
| `PhasmaCore` | static lib | Engine core — Vulkan, ECS, base utilities |
| `PhasmaEditor` | executable | Desktop editor (Windows/Linux) |
| `PhasmaAgent` | static lib | Standalone AI agent (no engine deps) |

### Key Singletons

| Singleton | Header | Purpose |
|---|---|---|
| `RHII` | `API/RHI.h` | Vulkan instance, device, queues, VMA memory |
| `Context` | `ECS/Context.h` | Global ECS manager — creates/destroys systems and entities |
| `EventSystem` | `Base/EventSystem.h` | Pub-sub event bus with `std::any` payloads |

### ECS

Systems derive from `ISystem` or `IDrawSystem` (`ECS/System.h`).
Components derive from `IComponent`.

```cpp
// Access patterns
entity->GetComponent<T>();
Context::CreateGlobalSystem<T>();   // in App.cpp boot sequence
ID::GetTypeID<T>();                 // type ID used as map key
```

Use `OrderedMap<size_t, T>` when deterministic iteration order matters.

### Boot Sequence (`App/App.cpp`)

1. SDL2 init
2. File watchers created (`FileWatcher`)
3. SDL window created
4. Global systems instantiated via `Context::CreateGlobalSystem<T>()`
5. Main loop: `UpdateGlobalSystems()` → `DrawGlobalSystems()` each frame

---

## Rendering Pipeline

Deferred Vulkan renderer using **dynamic rendering** — no `VkRenderPass` objects.

```
RendererSystem
  └── RenderGraph::AddPass(name, enableLambda)
        ├── IRenderPassComponent::DeclareInputs(RGBuilder&)
        ├── IRenderPassComponent::DeclareOutputs(RGBuilder&)
        └── IRenderPassComponent lifecycle:
              Init → CreateUniforms → UpdateDescriptorSets
              → Update → ExecutePass → Resize → Destroy
```

**Draw types** (in `Scene::Geometry`): Opaque, AlphaCut, AlphaBlend, Transmission.
Geometry is batched into unified vertex/index GPU buffers with indirect draw commands.

**Active passes**: GBuffer, Depth, Shadow (cascaded), Light (deferred), RayTracing,
Bloom, TAA, SSAO, SSR, DOF, MotionBlur, FXAA, Sharpen (RCAS), Tonemap, Grid, AABBs, Particles.

### Adding a New Render Pass

1. Create class inheriting `IRenderPassComponent`, implement all virtual methods
2. Register in `RendererSystem::BuildRenderGraph()` via `AddPass()`
3. Define push constants struct and descriptor layouts in the pass
4. Implement `DeclareInputs` / `DeclareOutputs` for resource dependency tracking
5. The pass automatically participates in resize events, descriptor updates, profiling timers

---

## Shaders

- HLSL sources: `PhasmaEditor/Assets/Shaders/`
- Compiled to SPIR-V via **DXC** (Windows) or **shaderc** (Linux) at build time and on hot-reload
- `FileWatcher` detects changes and broadcasts `EventType::CompileShaders`
- `ShaderCache` handles recompilation and on-disk caching
- Shared headers: `Assets/Shaders/Common/`
- Vulkan binding annotations: `[[vk::binding(N, Set)]]`, `[[vk::push_constant]]`

---

## Model Loading

All model loading routes through **Assimp** (`Scene/Model.cpp`).

```cpp
Scene::AddModel(model)           // registers model
Scene::UpdateGeometryBuffers()   // rebuilds unified GPU buffers
```

Vertex struct is **88 bytes** (position, uv, normal, tangent, color, joints, weights).
Always use the `FillVertex*` helpers — do not pack vertex data manually.

---

## PhasmaAgent Library (`PhasmaAgent/`)

Standalone AI agent library — **no Vulkan, no ImGui, no engine dependencies**.
Namespace: `pagent`.

### Key Classes

| Class | Header | Purpose |
|---|---|---|
| `Agent` | `Agent.h` | Public API — history, tools, provider backends, RAG |
| `IProviderBackend` | `Agent.h` | Implement to add a new LLM provider |
| `BM25Index` | `BM25Index.h` | Keyword search index; camelCase/snake_case tokenization; thread-safe |
| `VectorStore` | `VectorStore.h` | Embedding vector store; cosine similarity; thread-safe |
| `IncludeGraph` | `IncludeGraph.h` | C++ include dependency graph for context expansion |
| `CodebaseIndexer` | `CodebaseIndexer.h` | Indexes source files into BM25 + VectorStore |
| `CodebaseContext` | `CodebaseContext.h` | Owns VectorStore + BM25Index + async status checks; used by GUI |
| `RepoMap` | `RepoMap.h` | Compact codebase overview (~1K tokens) for system prompt |
| `EmbeddingUtils` | `EmbeddingUtils.h` | Factory: `CreateEmbeddingProvider(kind, model, key)` |
| `RagUtils` | `RagUtils.h` | Hybrid BM25 + vector retrieval helpers |

### Search APIs

```cpp
// Single-query search
bm25->Search(query, top_k);
store->Search(embedding, top_k, min_score);

// Multi-query parallel search (runs all queries in parallel, merges by max score)
bm25->SearchMulti(queries, top_k);
store->SearchMulti(embeddings, top_k, min_score);
```

### Agent Config

```cpp
AgentConfig config;
config.provider             = Provider::Anthropic;    // Anthropic / OpenAI / Google / Ollama / GoogleVertex
config.model                = "claude-sonnet-4-6";
config.system_prompt        = "...";
config.max_tokens           = 8192;
config.max_tool_rounds      = 10;
config.max_history_messages = 40;
config.summarize_after_messages = 20;    // auto-compact old history
config.summarize_tool_result_chars = 8000;
config.routing.enabled      = true;      // cheap model for simple queries
```

### Tool Handler Pattern

```cpp
agent.RegisterTool({
    .name = "my_tool",
    .description = "...",
    .properties = {
        {"param", "description", pagent::SchemaType::String, /*required=*/true},
    },
    .handler = [captures](const std::string& args) -> std::string {
        // Runs on WORKER THREAD — must be thread-safe.
        // Use ExtractArgStr / ExtractArgInt / ExtractArgArray from AgentUtils.h.
        // Return JSON string.
    }
});
```

### Agent Reasoning Loop

The intended design principle for all tools and features:

```
edit → search → build → fix
```

1. **edit** — modify source files (`patch_project_file`, `write_project_file`)
2. **search** — verify changes, find usages (`search_codebase`, `grep_project`, `find_symbol`)
3. **build** — compile and get structured error output
4. **fix** — use build errors to correct the edit and loop

### Registered Tools (EditorToolCatalog.cpp)

| Tool | Purpose |
|---|---|
| `execute_lua` | Run Lua in ScriptSystem for all scene manipulation |
| `read_project_file` | Read source file (with optional line range) |
| `write_project_file` | Write full file |
| `patch_project_file` | Replace line range (token-efficient — prefer over write) |
| `find_project_file` | Find files by name substring |
| `grep_project` | Regex/literal search across source files |
| `list_project_dir` | List directory contents |
| `find_symbol` | BM25 lookup for a single symbol name |
| `search_codebase` | Parallel multi-query BM25 + vector search (prefer over find_symbol for multiple lookups) |
| `find_loadable_model` | Find 3D model files in Assets/Objects/ |
| `read_agent_file` / `write_agent_file` | Read/write agent workspace files |
| `request_feature` / `complete_feature` | Track feature requests |
| `take_screenshot` | Capture editor state as base64 PNG |
| `query_imgui_windows` | Get visible ImGui panels with positions |
| `inject_mouse_input` | Simulate mouse input in the editor |
| `get_codebase_index_status` | Report RAG index state: has_index, is_indexing, progress, embeddings configured |
| `rebuild_codebase_index` | Start or restart the codebase BM25+vector index (`full_rebuild` param) |
| `cancel_codebase_index` | Cancel an in-progress indexing job |

---

## Coding Conventions

### Namespace
- Engine code: `namespace pe`
- Agent library: `namespace pagent`

### Naming
```
Classes/Structs:  PascalCase       (RenderGraph, BM25Index)
Methods:          camelCase        (getComponent, createPipeline)
Prefixes:         Get/Set/Create/Destroy
Member fields:    m_snake_case     (m_renderPass, m_vertexBuffer)
Static fields:    s_snake_case     (s_instance)
Macros/defines:   PE_UPPER_CASE
```

### Headers
- `#pragma once` — no include guards
- Forward-declare where possible, include in .cpp

### GPU Resources
Always use wrappers from `PhasmaCore/Code/API/`:
- `Buffer` — VMA-backed GPU buffer
- `Image` — VMA-backed image + view
- `Sampler` — Vulkan sampler wrapper
Never allocate raw `VkBuffer`, `VkImage`, `VkDeviceMemory`.

### Math
- GLM types: `vec3`, `vec4`, `mat4`, `quat`
- Left-handed coordinate system (Vulkan clip space)

### Error / Logging Macros
```cpp
PE_ERROR(fmt, ...)           // fatal error
PE_ERROR_IF(cond, fmt)       // conditional fatal
PE_ASSERT(cond, msg)         // debug assertion
PE_CHECK(vkResult)           // Vulkan result check
PE_INFO(fmt, ...)            // info log
PE_WARN(fmt, ...)            // warning log
```

### Precompiled Header — CRITICAL

`PhasmaCore/pch/PhasmaPch.h` is automatically included in every `.cpp` under `PhasmaCore/`
and `PhasmaEditor/` via CMake `target_precompile_headers`. It covers virtually all std headers.

**Do NOT add these includes to `.cpp` files under PhasmaCore/ or PhasmaEditor/:**
`<vector>`, `<string>`, `<map>`, `<unordered_map>`, `<set>`, `<mutex>`, `<thread>`,
`<functional>`, `<memory>`, `<filesystem>`, `<algorithm>`, `<optional>`, `<future>`,
`<atomic>`, `<chrono>`, `<sstream>`, `<fstream>`, `<iostream>`, `<regex>`, etc.

**Exception**: `PhasmaAgent/` has its own CMake target and does **not** use PhasmaPch.h.
Add explicit includes to all files under `PhasmaAgent/src/`.

### Code Quality Rules

- Extract logic into well-named functions; never write large inline blocks
- Reusable code must be shared — never copy-paste logic that already exists elsewhere
- Lambdas and callbacks should be short — delegate to named methods
- Prefer clear names and structure over brevity
- Code must compile and work on **both Windows and Linux**
  — use `PE_WIN32`/`PE_LINUX` guards only for genuinely platform-specific calls
  — always provide a Linux path when adding a Windows path

---

## Platform Macros

| Macro | Meaning |
|---|---|
| `PE_WIN32` | Windows |
| `PE_LINUX` | Linux |
| `PE_DEBUG` | Debug build |
| `PE_RELEASE` | Release build |
| `PE_RELWITHDEBINFO` | RelWithDebInfo |
| `PE_MINSIZEREL` | MinSizeRel |
| `PE_TRACK_RESOURCES` | Always defined |

---

## Dependencies (all via FetchContent — never add manual binaries)

| Library | Version | Used by |
|---|---|---|
| Assimp | v6.0.4 | PhasmaEditor — model loading |
| MeshOptimizer | v0.22 | PhasmaEditor — mesh optimization |
| Jolt Physics | v5.2.0 | PhasmaCore — physics simulation |
| SDL2 | release-2.30.9 | PhasmaCore, PhasmaEditor |
| SPIRV-Cross | sdk-1.3.296.0 | PhasmaCore |
| nlohmann/json | (pinned) | PhasmaAgent |
| cpp-httplib | v0.28.0 | PhasmaAgent — HTTP client |

---

## Commit Style

- Short imperative subject lines: `Add RenderGraph callbacks`, `Fix model loading crash`
- Keep commits focused — separate refactors from behavior changes
- Do NOT add `Co-Authored-By` lines to commits
- Update `AI_CHANGELOG.md` with a dated entry when finishing a session

---

## Things That Will Break the Build

- Adding `#include <std-header>` in `.cpp` under `PhasmaCore/` or `PhasmaEditor/` (PCH conflict)
- Using raw Vulkan allocations instead of the `Buffer`/`Image`/`Sampler` wrappers
- Adding binary library files manually instead of using FetchContent
