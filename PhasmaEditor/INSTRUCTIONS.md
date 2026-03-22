# PhasmaEditor — Instructions

Supplements the root `INSTRUCTIONS.md`. Read that file first.

PhasmaEditor is the **desktop editor executable** — links PhasmaCore, Assimp, MeshOptimizer,
Jolt Physics, ImGui, and the Lua scripting runtime.
Every `.cpp` here gets `PhasmaCore/pch/PhasmaPch.h` as a precompiled header.

---

## Directory Map

```
PhasmaEditor/Code/
├── App/
│   └── App.cpp                    # Boot: SDL2 → watchers → window → systems → main loop
├── Systems/
│   ├── RendererSystem.h/.cpp      # Coordinates render targets, render graph, frame sync
│   ├── LightSystem.h/.cpp         # Directional + point lights, cascaded shadow maps
│   ├── CameraSystem.h/.cpp        # Camera entity, view/proj, frustum culling
│   └── ScriptManager.h/.cpp       # Lua script watcher + execution
├── RenderPasses/
│   ├── GBufferPass.h/.cpp         # Deferred GBuffer: albedo, normal, roughness/metallic
│   ├── DepthPass.h/.cpp           # Pre-Z depth pass
│   ├── ShadowPass.h/.cpp          # Cascaded shadow map generation
│   ├── LightPass.h/.cpp           # Deferred lighting accumulation
│   ├── BloomPass.h/.cpp           # Threshold + Gaussian blur bloom
│   ├── TAAPass.h/.cpp             # Temporal anti-aliasing
│   ├── SSAOPass.h/.cpp            # Screen-space ambient occlusion
│   ├── SSRPass.h/.cpp             # Screen-space reflections
│   ├── DOFPass.h/.cpp             # Depth of field (bokeh)
│   ├── MotionBlurPass.h/.cpp
│   ├── FXAAPass.h/.cpp
│   ├── SharpenPass.h/.cpp         # RCAS sharpening
│   ├── TonemapPass.h/.cpp         # HDR → LDR (ACES / Reinhard)
│   ├── GridPass.h/.cpp            # Editor infinite grid
│   ├── AABBPass.h/.cpp            # Debug AABB visualization
│   └── ParticlePass.h/.cpp
├── Scene/
│   ├── Scene.h/.cpp               # Owns Geometry, manages models, scene save/load
│   ├── Model.h/.cpp               # Assimp model loading, node hierarchy
│   ├── Geometry.h/.cpp            # Unified GPU vertex/index buffers + indirect draw
│   └── PhysicsHelper.h/.cpp       # Creates Jolt shapes from node AABBs
├── GUI/
│   ├── GUI.h/.cpp                 # ImGui frame begin/end, widget orchestration, MCP codebase ownership
│   ├── Agent/
│   │   ├── EditorToolServer.h/.cpp      # In-process MCP HTTP server (port 8765, JSON-RPC 2.0)
│   │   ├── EditorToolRuntime.h/.cpp     # Main-thread-safe editor action bridge (QueueAction)
│   │   ├── EditorToolCatalog.h/.cpp     # All editor/MCP tool definitions
│   │   └── EditorToolSchemaUtils.h/.cpp # JSON schema helpers for tool definitions
│   └── Widgets/
│       ├── SceneWidget.h/.cpp     # Scene hierarchy panel
│       ├── PropertiesWidget.h/.cpp# Node inspector (transform, material, physics)
│       ├── FileBrowser.h/.cpp     # Asset browser
│       ├── PhysicsWidget.h/.cpp   # Physics body inspector section
│       └── ...\
└── Script/
    └── (Lua binding files)

PhasmaEditor/Assets/
├── Shaders/
│   ├── Common/                    # Shared HLSL headers (lighting, PBR, etc.)
│   └── *.hlsl                     # Per-pass vertex + pixel + compute shaders
├── Objects/                       # 3D model files
└── Agent/                         # In-engine agent workspace
    ├── START.md                   # Agent session instructions
    ├── MEMORY.md                  # Persistent agent notes
    └── TASKS.md                   # Agent task list
```

---

## Render Pass Implementation Pattern

```cpp
class MyPass : public IRenderPassComponent
{
public:
    void Init() override;
    void CreateUniforms() override;
    void UpdateDescriptorSets() override;
    void DeclareInputs(RGBuilder& builder) override;
    void DeclareOutputs(RGBuilder& builder) override;
    void Update(CommandBuffer* cmd) override;   // per-frame CPU update
    void ExecutePass(CommandBuffer* cmd) override;
    void Resize() override;
    void Destroy() override;

private:
    Buffer* m_uniformBuffer = nullptr;
    Image*  m_outputImage   = nullptr;
    struct PushConstants { /* per-draw data */ } m_pc;
};
```

Register in `RendererSystem::BuildRenderGraph()`:
```cpp
m_renderGraph->AddPass("MyPass", [this]{ return m_myPassEnabled; }, &m_myPass);
```

---

## Scene / Model Workflow

```cpp
// Load a model
auto* model = new Model();
model->Load("Assets/Objects/MyModel.glb");
Scene::AddModel(model);
Scene::UpdateGeometryBuffers();  // rebuilds unified GPU buffers

// Access nodes
for (auto& node : model->GetNodes())
{
    auto aabb = node.GetWorldAABB();
    // node.localMatrix, node.worldMatrix, node.name, node.meshIndex...
}
```

---

## Editor MCP Tool Registration

All editor/MCP tools are defined in `EditorToolCatalog.cpp` and served to external AI clients
(Claude Code, Claude Desktop, Codex) via `EditorToolServer` at `http://127.0.0.1:8765/mcp`.

The `projectRoot` variable is the canonical repo root — all file paths are relative to it.

Tool handler contract:
- Runs on the **httplib worker thread** — must be thread-safe
- Receives raw JSON args string
- Returns raw JSON result string
- Use helpers from `PhasmaAgent/include/PhasmaAgent/AgentUtils.h`:
  `ExtractArgStr`, `ExtractArgInt`, `ExtractArgArray`, `JsonStr`, `JsonObj`, `IsPathSafe`
- **Main-thread-only operations** (ImGui reads, scene mutation) must use `gui->QueueMainThreadAction(fn)` — never access GUI or engine state directly from the handler

Example:
```cpp
pagent::ToolDefinition tool;
tool.name        = "my_tool";
tool.description = "...";
tool.properties  = {
    {"path", "File path relative to project root", pagent::SchemaType::String, true},
};
tool.handler = [projectRoot](const std::string& args) -> std::string {
    std::string path = pagent::JsonUnescape(pagent::ExtractArgStr(args, "path"));
    if (!pagent::IsPathSafe(path, projectRoot))
        return "{\"error\":\"path outside project\"}";
    // ...
    return nlohmann::json{{"result", "ok"}}.dump();
};
// Add to tools vector in the appropriate Append*Tools function:
tools.push_back(std::move(tool));
```

---

## Shader Authoring

Shaders are HLSL, compiled to SPIR-V.

```hlsl
// Descriptor binding
[[vk::binding(0, 0)]] ConstantBuffer<MyUBO> ubo : register(b0, space0);
[[vk::binding(1, 0)]] Texture2D<float4> albedoTex : register(t1, space0);
[[vk::binding(2, 0)]] SamplerState linearSampler : register(s2, space0);

// Push constants
[[vk::push_constant]] struct { float4x4 mvp; } pc;
```

Hot-reload: edit any `.hlsl` file while the editor runs → FileWatcher triggers
`EventType::CompileShaders` → ShaderCache recompiles and reloads the pass automatically.

---

## GUI Rules (ImGui)

- ImGui is linked only for PhasmaEditor, not PhasmaCore or PhasmaAgent

---

## Lua Scripting

Scene manipulation from the AI agent or script files goes through Lua:

```lua
-- Load a model
load_model("Objects/DamagedHelmet/DamagedHelmet.glb")

-- Camera
camera_set_position(0, 2, 5)
camera_look_at(0, 0, 0)

-- Lights
set_sun_direction(0.5, -1, 0.3)
set_sun_intensity(3.0)

-- Scene
save_scene("MyScene")
load_scene("MyScene")
```

All Lua bindings are registered in `Script/` files and executed via `ScriptSystem::ExecuteLua()`.
