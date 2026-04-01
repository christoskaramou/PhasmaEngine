# PhasmaEditor — Instructions

Supplements the root `INSTRUCTIONS.md`. Read that file first.

PhasmaEditor is the **desktop editor executable** — links PhasmaCore, Assimp, MeshOptimizer,
Jolt Physics, ImGui, and the Lua scripting runtime.
Every `.cpp` here gets `PhasmaCore/pch/PhasmaPch.h` as a precompiled header.

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

## Scene / ModelAsset Workflow

```cpp
// Load a model
ModelAsset* model = ModelAsset::Load("Assets/Objects/MyModel.glb");
Scene::AddModel(model);
Scene::UpdateGeometryBuffers();  // rebuilds unified GPU buffers

// Access nodes through const API
int root = model->GetRootNodeIndex();
mat4 world = model->GetNodeWorldMatrix(root);
AABB bounds = model->GetNodeWorldBoundingBox(root);
const std::string& name = model->GetNodeName(root);

// Mutate through explicit setters
model->SetNodeLocalMatrix(root, newMatrix);
```

**Data separation**: `NodeInfo` holds logical data (parent, children, localMatrix, name).
`NodeRuntimeInfo` holds GPU/renderer state (worldMatrix, dirtyFlags, boundingBox, dataOffset).
`MeshInfo` holds geometry/material data. `MeshRuntimeInfo` holds image view indices.

---

## Editor MCP Server

The MCP server runs at `http://127.0.0.1:8765` (localhost only, no external access).

### OAuth (required by Claude Code and MCP-spec-compliant clients)

Claude Code enforces OAuth for `"type": "http"` MCP servers. The editor implements minimal stub
OAuth endpoints that return a static 10-year token — no real authentication, just enough to satisfy
the spec:

| Endpoint | Purpose |
|---|---|
| `GET /.well-known/oauth-authorization-server` | RFC 8414 metadata discovery |
| `POST /oauth/register` | RFC 7591 dynamic client registration |
| `GET /oauth/authorize` | Auth code flow — immediate redirect with static code |
| `POST /oauth/token` | Returns static bearer token, `expires_in: 315360000` |

On first connect, the client completes the flow automatically (no browser/user interaction needed)
and stores the token. Subsequent sessions connect without prompting.

These endpoints are implemented in `EditorToolServer::ConfigureRoutes()`.

### Connecting from Claude Code (Windows)

MCP tools are injected at **session start only**. Start the editor before starting a Claude Code
session, then `mcp__phasmaeditor__*` tools are available immediately. If the editor starts after
the session, use `/mcp` to connect — then start a **new session** for the tools to appear.

### Connecting from Codex (WSL)

Codex runs inside WSL2, which has its own network namespace — `127.0.0.1` inside WSL does **not**
reach the Windows-side server. Do **not** work around this with manual HTTP calls or `powershell.exe`
wrangling. Instead, set up native MCP tool injection once:

**Step 1 — enable WSL mirrored networking** (one-time, requires WSL 2.0+ / Windows 11 22H2+):

Create or edit `C:\Users\<USERNAME>\.wslconfig`:
```ini
[wsl2]
networkingMode=mirrored
```
Then restart WSL: `wsl --shutdown`. After this, `127.0.0.1` inside WSL resolves to the Windows
loopback, so the editor is reachable at `http://127.0.0.1:8765/mcp`.

**Step 2 — add MCP config to Codex** (one-time):

Add to `~/.codex/config.toml` inside WSL:
```toml
[mcp_servers.phasmaeditor]
url = "http://127.0.0.1:8765/mcp"
```

After both steps, start the editor, then start a Codex session — `mcp__phasmaeditor__*` tools
are injected natively at session start, identical to how Claude Code connects.

### Tool Registration

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

Scene manipulation from the AI agent or script files goes through Lua.
All Lua bindings are registered in `Script/Bindings/` files and executed via `ScriptSystem::ExecuteLua()`.

### File-Level Scripts

Scripts placed in `Assets/Scripts/` are loaded as file-level scripts with shared global state.
Non-hook globals are promoted so other scripts can access them (e.g. utility libraries).

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

### Per-Node Scripts (Component_Script)

When a `.lua` script is attached to a scene node (via `Component_Script`), it runs in an
**isolated `sol::environment`** — no shared state with other nodes, even if they reference the
same `.lua` file. Node-attached scripts are excluded from the file-level loading path entirely.

Each per-node instance gets these variables injected automatically (refreshed every frame):

| Variable | Type | Description |
|---|---|---|
| `self` | `SceneNodeHandle` | The node itself — `get_name()`, `set_name()`, `get_parent()`, `get_children()`, etc. |
| `transform` | `SceneNodeHandle` | Same handle — `get_position()`, `set_position()`, `get_rotation()`, `set_rotation()`, `get_scale()`, `set_scale()`, `set_transform()` |
| `mesh` | table or nil | `{ index, vertex_count, index_count, bounding_box }` if the node has `Component_Mesh`, else `nil` |
| `camera` | `Camera*` or nil | Camera userdata if the node has `Component_Camera`, else `nil` |

Lifecycle hooks are declared with `hooks{}`:

```lua
hooks {
    init = function()
        pe_log("Node: " .. self:get_name())
    end,
    update = function()
        local pos = transform:get_position()
        pos.y = pos.y + 0.01
        transform:set_position(pos)
    end,
    update_editor = function()
        -- Runs every frame even outside play mode
    end,
    destroy = function()
        pe_log("Cleaning up")
    end
}
```

Exposed variables create editor-editable properties in the Properties panel:

```lua
local props = exposed {
    speed = 5.0,
    enabled = true,
    label = "hello"
}
-- props.speed always reflects the current editor value
```

### Cross-Script Communication

Per-node scripts are fully isolated, but `exposed{}` variables act as a public interface.
Other scripts can access them via `get_exposed()` on any `SceneNodeHandle`, which returns
the actual `__exposed` table (a live reference, not a copy):

```lua
-- Player node script
local props = exposed { health = 100, name = "Player1" }

-- Enemy node script (reads/writes player's exposed vars directly)
hooks {
    update = function()
        local player = scene_find("Player")
        if player and player:is_valid() then
            local vars = player:get_exposed()  -- live reference to Player's exposed table
            if vars and vars.health < 50 then
                vars.health = vars.health + 10 -- directly modifies Player's value
            end
        end
    end
}
```

Only variables declared via `exposed{}` are accessible. Local variables remain private.
`get_exposed()` returns `nil` if the node has no script or no `exposed{}` declaration.

### Key Implementation Details

- `ScriptSystem::ReconcileNodeInstances()` — creates/destroys per-node instances as nodes gain/lose `Component_Script`
- `ScriptSystem::RefreshNodeInstanceBindings()` — updates `self`/`transform`/`mesh`/`camera` each frame
- `ScriptSystem::FindNodeInstance(node)` — looks up the instance for a specific node (used by Properties panel and `get_script_var`/`set_script_var`)
- Node scripts use `SceneNodeHandle` (generation-counted) for safe references across scene reloads
