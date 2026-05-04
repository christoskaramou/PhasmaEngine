# PhasmaEditor — Instructions

Supplements the root `INSTRUCTIONS.md`. PhasmaEditor is the desktop editor executable — links PhasmaCore, Assimp, MeshOptimizer, Jolt, ImGui, Lua. All editor code lives in `PhasmaEditorModule.dll` (hot-reloaded); `PhasmaEditor.exe` is a thin launcher.

For render-pass / scene / model-loading code patterns, read the actual files in `Code/RenderPasses/`, `Code/Scene/`, `Code/Systems/`. The patterns don't rot when you read source directly.

---

## Editor MCP server

The MCP server runs at `http://127.0.0.1:8765` (loopback only). Implementation: `Code/GUI/Agent/EditorMcp.cpp` (wraps `pmcp::Server` + `pmcp::HttpTransport`). Tools are defined in `EditorToolCatalog.cpp`.

### OAuth shim — what it is and why it's not real auth

Claude Code refuses to attach to HTTP MCP servers without OAuth metadata, so the editor opts in to `pmcp::HttpTransportConfig::enableLocalOauthShim`. This publishes minimal stub endpoints returning a static, non-secret bearer token:

| Endpoint | Purpose |
|---|---|
| `GET /.well-known/oauth-authorization-server` | RFC 8414 metadata discovery |
| `POST /oauth/register` | RFC 7591 dynamic client registration |
| `GET /oauth/authorize` | Auth code flow — immediate redirect with static code |
| `POST /oauth/token` | Returns static bearer token, `expires_in: 315360000` |

**The shim is not authentication.** Real safety comes from two structural invariants enforced by the transport:

1. `Start()` refuses to bind to a non-loopback address while the shim is enabled.
2. `/mcp` and `/tool` reject non-local `Origin` (browser-CSRF defense).

The shim defaults to **off** in `HttpTransportConfig` so reusable adopters of `PhasmaMCP` don't inherit a fake auth surface; the editor opts in explicitly.

### Tool handler contract

- Runs on the **transport worker thread** — must be thread-safe.
- Receives `(const nlohmann::json& args, pmcp::Context& ctx)`.
- Returns `pmcp::CallToolResult` (`Json` / `Text` / `Error` / `ImageBase64` helpers).
- Path safety: `pmcp::IsPathSafe(path, projectRoot)`.
- **Main-thread-only operations** (ImGui reads, scene mutation) must use `gui->QueueMainThreadAction(fn)` — never touch GUI/engine state directly from the worker.

### Connecting

- **Claude Code (Windows):** start the editor, then start the session. MCP tools are injected at session start only.
- **Codex (WSL):** WSL2 needs mirrored networking (`networkingMode=mirrored` in `~/.wslconfig`, then `wsl --shutdown`). Then add `[mcp_servers.phasmaeditor] url = "http://127.0.0.1:8765/mcp"` to `~/.codex/config.toml`. After both, start editor → start Codex session.

---

## Lua scripting — per-node isolation

When a `.lua` script is attached to a scene node (via `Component_Script`), it runs in an **isolated `sol::environment`** — no shared state with other nodes, even if they reference the same `.lua` file. Node-attached scripts are excluded from the file-level loading path entirely.

Each per-node instance gets these variables injected automatically (refreshed every frame):

| Variable | Type | Description |
|---|---|---|
| `self` | `SceneNodeHandle` | The node — `get_name()`, `set_name()`, `get_parent()`, `get_children()`, etc. |
| `transform` | `SceneNodeHandle` | Same handle — `get_position()`, `set_position()`, `get_rotation()`, `set_rotation()`, `get_scale()`, `set_scale()`, `set_transform()` |
| `mesh` | table or nil | `{ index, vertex_count, index_count, bounding_box }` if the node has `Component_Mesh`, else `nil` |
| `camera` | `Camera*` or nil | Camera userdata if the node has `Component_Camera`, else `nil` |

### Hooks

```lua
hooks {
    init    = function() pe_log("Node: " .. self:get_name()) end,
    update  = function() local p = transform:get_position(); p.y = p.y + 0.01; transform:set_position(p) end,
    update_editor = function() end,  -- runs every frame even outside play mode
    destroy = function() end,
}
```

### Exposed variables (editor-editable)

```lua
local props = exposed { speed = 5.0, enabled = true, label = "hello" }
-- props.speed always reflects the current editor value
```

### Cross-script communication

Per-node scripts are isolated, but `exposed{}` variables act as a public interface. Other scripts reach them via `get_exposed()` on any `SceneNodeHandle` — returns a **live reference** to the `__exposed` table (not a copy):

```lua
local player = scene_find("Player")
if player and player:is_valid() then
    local vars = player:get_exposed()
    if vars and vars.health < 50 then vars.health = vars.health + 10 end
end
```

`get_exposed()` returns `nil` if the node has no script or no `exposed{}` declaration. Local variables remain private.

### Implementation hooks

- `ScriptSystem::ReconcileNodeInstances()` — creates/destroys instances as nodes gain/lose `Component_Script`.
- `ScriptSystem::RefreshNodeInstanceBindings()` — updates `self` / `transform` / `mesh` / `camera` each frame.
- `ScriptSystem::FindNodeInstance(node)` — instance lookup (used by Properties panel and `get_script_var` / `set_script_var`).
- `SceneNodeHandle` is generation-counted — safe across scene reloads.
