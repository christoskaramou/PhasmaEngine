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

All model loading routes through **Assimp** (`Scene/ModelAssetAssimp.cpp`).
`ModelAsset` is the base asset class; `ModelAssetAssimp` is the Assimp-based loader.

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

---

## Scene / ECS Architecture

### Node System

Scene nodes use a full ECS architecture. Each node is a `NodeId*` (stable handle with revision) backed by an `Entity*` in `Context`.

Node state lives in ECS components:
- `NodeNameComponent`, `NodeHierarchyComponent`, `NodeTransformComponent`
- `NodeMeshRefsComponent` — `vector<int>` mesh references (0..N per node)
- `NodeScriptComponent`

Subsystem presence uses tags: `NodeCameraTag`, `NodeLightTag`, `NodePhysicsTag`, `NodeAudioTag`.

`ComponentFlags` are derived from ECS cache presence, not stored explicitly.

`m_nodeRuntime` is a dense renderer sidecar (GPU offsets, dirty bits, cached state) — it is **NOT** ECS state.

### Cameras and Lights

Cameras and lights are first-class scene nodes carrying `Component_Camera` or `Component_Light` flags. They live anywhere in the hierarchy — no separate "Cameras" or "Lights" sections.

**Transform authority:**
- Camera: `Camera::m_position` / `m_euler` are authoritative → `Scene::Update()` writes these to the node local matrix each frame.
- Light: node local/world matrix is authoritative → `LightSystem::Update()` reads node world matrix → light pos/rot each frame.

When a camera node is moved via gizmo, `SceneView::ApplySelectionContext` must also call `Camera::SetPosition/SetEuler` or `Scene::Update` will immediately overwrite the gizmo result.

Both `SelectionType::Camera` and `SelectionType::Light` are dead-code fallbacks — all current paths use `SelectionType::Node`.

---

## Material System

### Overview

All material data flows through first-class `Material*` on `Mesh`/`MeshInfo`. No legacy per-mesh material fields exist.

**Ownership:**
- `Scene::m_ownedMaterials` — materials from `AssimpLoader` (scene-level loading)
- `ModelAsset::m_materials` — materials from `ModelAssetAssimp` / Primitives
- `Scene::m_ownedMaterialInstances` — instances created when user marks a mesh as "Instanced"

**Sharing:** Both loaders dedup by `aiMaterialIndex`. GPU material table deduplicates by `Material*` pointer identity.

**MaterialInstance:** `MaterialInstance(Material* parent)` stores optional overrides. `Resolve()` merges parent + overrides into a stack-allocated `Material`. `ApplyDiffFromResolved()` sets overrides by diffing against parent — used by the serializer on load.

**GPU updates:** `Material::dirty` flag triggers `Scene::UpdateDirtyMaterials()` — partial buffer write at the material's existing `gpuIndex`. No buffer recreation, no GPU stall. `UpdateTextures()` is used for texture swaps and structural changes only.

To change transmission material appearance, set `Material::ior`, `thicknessFactor`, `attenuationDistance`, `attenuationColor` then call `Scene::SetMaterialDirty()`. Any code path that modifies `mesh.material->*` fields must call `s->SetMaterialDirty()` — `MarkNodeDirty()` alone does not flush material data to the GPU.

### MaterialGpuData layout (80 bytes, 5 × vec4)

Must match `Structures.hlsl` exactly:

```cpp
struct MaterialGpuData {
    vec4 baseColorFactor;       // rgba
    vec4 emissiveTransmission;  // xyz=emissive, w=transmissionFactor
    vec4 pbrParams;             // x=metallic, y=roughness, z=alphaCutoff, w=occlusionStrength
    vec4 transmissionVolume;    // x=thicknessFactor, y=attenuationDistance, z=ior, w=unused
    vec4 attenuationColor;      // xyz=attenuationColor, w=unused
};
```

### IOR / Transmission

Assimp reads `AI_MATKEY_REFRACTI` → `KHR_materials_ior` extension in glTF. Default 1.5 if absent. Both loaders (`ModelAssetAssimp.cpp` and `AssimpLoader.cpp`) read it.

---

## Physics (Jolt v5.2.0)

`PhysicsSystem` manages body lifecycle, fixed-timestep simulation (1/60s — actually reduced to 1/30s with 2 substeps for perf), raycasts, velocity/force/impulse API.

- Bodies added via `AddBody(scene, node, desc)`.
- Play/Stop wired to `StartSimulation` / `StopSimulation`.
- `authoredScale` cached per body for fast `SyncTransformsFromJolt`; invalidated by `NotifyScaleChanged`.
- `cachedShape` reused across play toggles; invalidated by `InvalidateShapeCache` when desc changes.
- Lua: `physics.add_body`, `remove_body`, `set_velocity`, `apply_force`, `apply_impulse`, `raycast`, `is_simulating`.

---

## Audio & Animation

**Audio:** `AudioSystem` handles sound loading and playback. Nodes get `Component_Audio` / `NodeAudioTag`. Serialized in `TakeSnapshot` / `RestoreSnapshot` / `SaveScene` / `LoadScene`.

**Animation:** `AnimationSystem` with per-node state; `AnimationEvaluator::EvaluatePose()` does lerp/slerp hierarchy evaluation. GPU skinning: 4-bone blend in GbufferVS/DepthVS/ShadowVS; joint matrices in storage buffer. Animation state survives hot-reload (serialized in `TakeSnapshot` / `RestoreSnapshot`).

---

## Hot Reload (Module Architecture)

`PhasmaEditor` exe is a thin launcher. All editor code lives in `PhasmaEditorModule.dll` (Windows) / `libPhasmaEditorModule.so` (Linux).

**What stays in exe / PhasmaCore.dll (never reloaded):**
- `RHII` — Vulkan device, queues, allocator, swapchain
- `Context` — ECS entity/component storage
- `EventSystem`, `FileWatcher`, `ThreadPool`, `Settings`
- SDL window handle, ImGui context pointer

**What is in the reloadable module:**
- All files under `PhasmaEditor/Code/` — `RendererSystem`, all render passes, `Scene`, `ScriptSystem`, `GUI`, `UndoRedo`, all widgets

**Triggering a reload:** `File > Reload Module` (GUI), `engine.reload_module()` (Lua), or `reload_module` MCP tool. Scene state (geometry, materials, transforms, physics, audio, animation, UI widget open states) is fully preserved via `TakeSnapshot` / `RestoreSnapshot`.

**Phase status (as of 2026-04-04):**

| Phase | Description | Status |
|-------|-------------|--------|
| 0 | TypeID fix — `__FUNCSIG__` string hash, stable across DLL boundaries | Done |
| 1 | Thin launcher exe — loads module, ticks it, reloads on event | Done |
| 2 | Move all editor code to `PhasmaEditorModule` CMake SHARED target | Done |
| 3 | State bridge — serialize/restore scene across reload; `reload_module` MCP tool | Done |
| 4 | GPU safety + ImGui — `WaitAllFramesCommands()` before unload; ImGui context forwarding | **TODO** |
| 5 | Build orchestration — FileWatcher on sources → `cmake --build --target PhasmaEditorModule` → reload | **TODO** |

**Critical implementation notes:**
- `ImGui::GetCurrentContext()` must be forwarded to the module on load; call `ImGui::SetCurrentContext(ctx)` in module init.
- EventSystem callbacks: module must unregister all callbacks in its destroy function; dangling callbacks crash.
- Windows: use versioned copy before `LoadLibrary` (`HotReload/PhasmaEditorModule_NNNN.dll`) to avoid file lock.
- Safe reload window: inside `Window::ProcessEvents()` after `WaitAllFramesCommands()`, before `UpdateGlobalSystems()`.

---

## PhasmaCore DLL

PhasmaCore is a **SHARED** library (`PhasmaCore.dll`). `WINDOWS_EXPORT_ALL_SYMBOLS ON` — most symbols export automatically. Only 4 things require explicit `PE_API`:

- `RHI.h` — `extern RHI &RHII`
- `ThreadPool.h` — singleton instances
- `Pipeline.h` — static blend state definitions
- `Settings.h` — `extern template PE_API GlobalSettings &Settings::Get<GlobalSettings>()`

---

## Scripting

Per-node Lua scripts are isolated: each `Component_Script` node gets its own `sol::environment`. Nodes with the same `.lua` file do not share state.

- `ScriptSystem::CreateNodeInstance()` creates the isolated env inheriting globals.
- `RefreshNodeInstanceBindings()` injects `self`, `transform`, `mesh`, `camera` each frame.
- `exposed{}` declares public variables editable in the Properties panel.
- `get_exposed()` returns the live table (writes persist), or `nil` for nodes without scripts.
- MCP/editor Lua execution uses the **main** `ScriptSystem` via `GetGlobalSystem<ScriptSystem>()`. Do not cache a `ScriptSystem*` in `EditorToolRuntime`.
- Missing script files degrade to `PE_WARN` + early return — non-fatal; editor keeps running.

---

## Known Pitfalls

### SafeFloat destroys infinity

`SafeFloat()` converts `+inf` to `0.0`. `Material::attenuationDistance` is a valid field where infinity means "no attenuation limit." Never pass it through `SafeFloat()`. On load, treat non-positive attenuation distance as infinity for backward compat with old saves.

### ResourceHandle copy can crash

Copying a `Material` object copies `ResourceHandle<Image>` shared_ptrs for all 5 textures. If any has a corrupted control block, the refcount increment crashes with access violation. In hot paths (e.g., `UpdateImageViews`), access textures via `const Material* mat = mesh.material; mat->textures[k].get()` — never copy the full `Material`.

### GbufferPass PassInfo lazy init

`GbufferOpaquePass::Init()` must guard shared `PassInfo` creation with `if (!m_passInfoDS)`. If `Init()` runs unconditionally during a `Resize()` cycle, the new `PassInfo` has null shaders (because `UpdatePassInfo()` is not called from `Resize()`), causing a null dereference on the next draw. Apply the same `if (!ptr)` guard to any render pass that creates additional `PassInfo` variants.

### `scene.clear()` is asynchronous

`scene.clear()` pushes a `ModelsRemoved` event processed on the next frame. Do not immediately follow it with `scene.load()` in MCP/Lua without giving the engine a frame to process removals — doing so can cause a crash ~10 seconds later due to the async removal racing with the sync load.

---

## MCP / Editor Testing

### Enabling MCP

Set `"mcp": true` in `PhasmaEditor/Assets/Agent/agent_config.json`. Rebuild so the post-build asset copy picks it up.

### Launching the Editor

```bash
cd c:/Users/Christos/repos/PhasmaEngine/build/Debug && start PhasmaEditor.exe
```

MCP tools are loaded at session start only. If the editor was not running when the agent session began, start a new session with the editor already running — `mcp__phasmaeditor__*` tools will be available from the first message.

### Crash Detection

Lua `pcall` catches Lua-level errors but NOT C++ access violations. If `mcp__phasmaeditor__execute_lua` returns "Unable to connect", the editor has crashed. Check liveness:

```bash
sleep 10
tasklist | grep -i phasma   # empty = crashed
```

Force-exit: `taskkill //F //IM PhasmaEditor.exe`

### MCP Tools Reference

| Tool | Purpose |
|------|---------|
| `execute_lua` | Run Lua in ScriptSystem for all scene manipulation |
| `profiler_snapshot` | Save profiler JSON with fps, frame_ms, GPU pass breakdown, CPU scopes, memory |
| `get_console_log` | Recent log entries; params: `count`, `level` |
| `take_screenshot` | Base64 PNG of editor window |
| `query_imgui_windows` | All visible ImGui panels with positions/sizes and tab click coordinates |
| `inject_mouse_input` | Simulate mouse clicks/moves in editor UI |
| `search_codebase` | Parallel multi-query BM25 + vector search (prefer over `find_symbol` for multiple lookups) |
| `find_symbol` | BM25 lookup for a single symbol name |
| `grep_project` | Regex/literal search across source files |
| `find_project_file` | Find files by name substring |
| `find_loadable_model` | Find 3D model files in `Assets/Objects/` |
| `list_project_dir` | List directory contents |
| `read_project_file` / `write_project_file` / `patch_project_file` | Read/write/patch project files |
| `read_agent_file` / `write_agent_file` | Agent workspace files |
| `get_codebase_index_status` / `rebuild_codebase_index` / `cancel_codebase_index` | RAG index management |
| `reload_module` | Hot reload `PhasmaEditorModule.dll` — serializes scene, unloads/reloads DLL, restores scene |
| `request_feature` / `complete_feature` | Track feature requests |

**Profiler note:** `profiler_snapshot` returns all zeroes if the Profiler panel has not been opened in the editor session. Use `engine.get_metrics()` via Lua for quick fps/delta_ms without the panel:

```lua
local m = engine.get_metrics()
pe_log("FPS: " .. m.fps .. " Delta: " .. m.delta_ms .. "ms")
local mem = rhi.get_gpu_memory()
pe_log("VRAM used: " .. mem.vram.used .. " budget: " .. mem.vram.budget)
```

### Key Lua APIs

- `primitives.sphere()`, `.cube()`, `.plane()`, `.cylinder()`, `.cone()`, `.quad()` — add primitives
- `scene.get_entities()` — returns table of `{label, node, type, is_primitive}`; `node` is a `SceneNodeHandle`
- `node:get_exposed()` — returns live exposed table (writes persist), or `nil` if no script
- `node:get_script()` / `node:set_script(path)` — get/set the Lua script on a node
- `node:get_position()` / `set_position(vec3)` / `get_rotation()` / `set_rotation(vec3)` / `get_scale()` / `set_scale(vec3)`
- `material.get(model, meshIndex)` / `material.set(model, meshIndex, "field", value)`
- `material.get_render_type(model, meshIndex)` / `material.set_render_type(model, meshIndex, "opaque"|"alpha_blend"|"alpha_cut"|"transmission")`
- `scene.save("name")` / `scene.load("name")` / `scene.clear()`
- `engine.reload_module()` — trigger hot reload from Lua

---

## Performance Testing

### Rules (apply every perf session)

1. **Scene:** Load `Assets/Scenes/sponza.pescene` via `scene.load("Scenes/sponza.pescene")`. Do not use `load_model_async` — the saved scene has the correct camera position.

2. **Build config:** Release only for performance comparisons (Debug has no optimizations — comparisons are meaningless). Use Debug only for crash investigation.

3. **Present mode:** After scene loads, call `rhi.change_present_mode("immediate")` then verify by reading `engine.get_metrics().fps`. If fps stays at ~60, re-call — the Lua call alone is not always sufficient on first attempt after relaunch.

4. **Screenshot before changes:** Take a screenshot before collecting any "after" snapshots for a visual fidelity reference.

5. **Snapshot cadence:** 10 snapshots spaced **1 second apart** (not 0.5s). 1s spacing gives more representative samples.

6. **Comparison:** `python3 tools/compare_snapshots.py baseline/ current/`. Baseline stays untouched; only `current/` changes between iterations.

7. **Loop:** Fix → `perf_cycle.sh Release` → reload sponza → set immediate → screenshot → 2s warmup → 10 snapshots → compare. Repeat until `compare_snapshots.py` exits 0.

### Regression Thresholds

- FPS: >5% drop AND >1 fps
- ms timings: >5% AND >0.5ms
- VRAM: >50 MB

### Benchmark Scripts

| File | Purpose |
|------|---------|
| `Assets/Scripts/bench/bench_setup.lua` | Scene setup helpers: `bench.setup_geometry(N)`, `.setup_rt()`, `.setup_alpha()` |
| `Assets/Scripts/bench/bench_collect.lua` | Quick metrics sampling: `bench_collect.sample(label)` → `[BENCH_SAMPLE]` log line |
| `tools/compare_snapshots.py` | Average N snapshots vs baseline, flag regressions, exit 0/1/2 |
| `tools/perf_cycle.sh [Release\|Debug]` | Kill editor → cmake build → relaunch → poll until MCP ready |

---

## Workflow Rules

- **Never commit without explicit instruction.** Stage files when asked, but do not run `git commit` unless the user says "commit" (or equivalent) in that message.
- **Never add `Co-Authored-By` lines** to commits.
- **Run `clang-format -i`** on every modified C++ source/header file after editing.
- **Codebase vector store** (`.bin` / VSTB format) is binary and not directly readable. Use Grep/Glob for code lookups.
