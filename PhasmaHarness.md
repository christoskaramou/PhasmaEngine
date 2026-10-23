# Project instructions

This is the shared entry point for every agent launched by Harness in this workspace.

## Working rules

- Inspect relevant live source before editing. Preserve unrelated changes. Run focused checks and report what was actually verified.
- Keep project rules in PhasmaHarness.md. Do not create parallel AGENTS.md, CLAUDE.md or other provider instruction files.

## Project knowledge

Use the active workspace wiki index supplied by Harness with each worker request (Settings > Wiki). Its location can change; do not create a competing wiki here. Search focused pages with project_context when available, then verify claims in live source. Do not load the whole wiki.

## Improving these instructions

After a recurring correction, verified pitfall, or workflow change, propose a small update with supporting source references and a verification date. Keep this entry short; put detailed architecture and decisions in the wiki. Remove obsolete or duplicate guidance when proposing replacements. Apply updates only when the user authorizes them. Do not store secrets, raw conversations, or session diaries.


Here `<engine-wiki>` means the engine knowledge folder linked from the active wiki index supplied by Harness (currently the imported engine wiki). Resolve that link before using the paths or commands below; it is a placeholder, not a literal path. This keeps rules valid in project copies and on other machines.

## Imported project rules

The sections below preserve existing project guidance. Apply scoped rules only inside that workspace subfolder; paths in those sections are relative to that scope. Deeper scopes override parent scopes. Maintain these rules here; old entry filenames and mirror-sync instructions refer to this consolidated file. The active wiki supplied by Harness replaces old docs/wiki pointers.

### Workspace scope: .

Imported from: .cursor/rules/against-the-hero-repo.mdc

---
description: AgainstTheHero lives in its own private repo, not PhasmaProjects
alwaysApply: true
---

# AgainstTheHero location

AgainstTheHero (ATH) is a **private** game project. Canonical paths:

- Local: `C:\Users\Christos\repos\AgainstTheHero`
- GitHub: https://github.com/christoskaramou/AgainstTheHero

Do **not** treat `PhasmaProjects/AgainstTheHero` as the source of truth for new ATH
work. That folder is legacy / transitional only.

When editing ATH gameplay, assets, or tools, work in `C:\Users\Christos\repos\AgainstTheHero`.

### Workspace scope: .

Imported from: .cursor/rules/no-co-authored-by.mdc

---
description: Never add Co-Authored-By lines to git commits
alwaysApply: true
---

# No Co-Authored-By

**Never add `Co-Authored-By` lines** to commits.

### Workspace scope: .

Imported from: .cursor/rules/no-push-unless-asked.mdc

---
description: Never git push unless the user explicitly requests it in that message
alwaysApply: true
---

# Git push — explicit only

**Hard rule:** Do not run `git push` unless the user explicitly says **push** (or equivalent: "push to remote", "push both repos", etc.) in that message.

- Commit when asked; stage when asked.
- After commit, stop and report — do not push by default.
- Applies to PhasmaEngine, PhasmaProjects, and every repo in this workspace.

### Workspace scope: .

Imported from: .cursor/rules/ponytail-code-quality.mdc

---
description: Hard rule — smallest correct solution and no perf regressions (ponytail full)
alwaysApply: true
---

# Code quality & performance (ponytail)

**Hard rule:** Prefer the smallest correct solution and protect performance on every change — not only when the user says "ponytail".

Climb the ponytail ladder (YAGNI → reuse codebase → stdlib/engine utils → platform/RHI → existing deps → one line → minimum diff). Trace the full flow first. Fix at the shared choke point. No unrequested abstractions. Deletion over addition.

Hot-path and render work: run perf gates (`PhasmaHarness.md` → `Rules — performance testing`) before claiming done. Profile before caches or speculative complexity.

Mark deliberate simplifications with `// ponytail:` (ceiling + upgrade path). Never ponytail away validation, security, data-loss prevention, or explicitly requested completeness.

Default posture: ponytail **full**. See `PhasmaHarness.md` for the full rule block.

### Workspace scope: .

Imported from: .github/copilot-instructions.md, AGENTS.md, CLAUDE.md, GEMINI.md, HERMES.md

# PhasmaEngine — Agent Instructions


`PhasmaHarness.md` is a dispatcher pointing to live code and the wiki — read it to know where to look for what. `<engine-wiki>/index.md` is the synthesis layer; find pages with `bash "<engine-wiki>/tools/search.sh" "query"`.

**Wiki sync:** before finishing a session that changed code a wiki page describes, update the page and run `bash "<engine-wiki>/tools/lint.sh"`.

## Project knowledge — wiki only

Use the project's `<engine-wiki>/index.md` as the single maintained project knowledge entry point. Search live source for implementation; consult focused wiki pages for decisions, architecture and pitfalls. Verify historical claims against current source. Record durable discoveries in the relevant wiki page with source references and a verification date; do not create routine session diaries.

MemPalace and Graphify are retired from the active workflow. Do not query, update, rebuild or recreate their stores or invoke their hooks. Historical archives are for explicit recovery only. Keep personal preferences in global instructions, not a second project memory database. Do not automatically read entire wiki pages or maintenance logs when a focused search is enough.

## Rules — commits & workflow

- **Never commit autonomously.** Stage when asked, but do not run `git commit` unless the user explicitly says "commit" (or equivalent) in that message.
- **Never add `Co-Authored-By` lines** to commits.
- **Every commit message is a single line** — subject only, no body.
- **Work on `master` uncommitted by default** — no worktrees, all changes left as unstaged diffs for the user to review. Exception: long-running carve work on the `DX12` branch may commit; check the active branch.
- **Run `clang-format -i`** on every modified `.cpp` / `.h` after editing.
- **Before writing a commit message, run `git diff --staged`** (or `git diff`). Never write commit text from memory.

## Rules — session end

When the user signals a session is wrapping ("ready for new session", "we're done"), do this without being asked:

1. Review touched files; remove comments that aren't load-bearing.
2. `clang-format -i` every modified `.cpp` / `.h`.
3. Draft a short imperative commit message (do **not** commit).
4. Produce a paste-ready prompt for the next session: branch + engine commit + unstaged files, current state bullets, priority-ranked next targets, build/test commands.

## Rules — code hygiene

- **No test-only engine code in the tracked repo.** Local-repro hooks (env vars, debug toggles, CTS-only branches) stay local; do not commit them even when inert at runtime. Production-relevant feature work (advertising / gating optional features) is unaffected — that does belong in the engine.
- **`third_party/` is off-limits.** Never edit any `third_party/` directory. If an extension is needed, relocate the crate out of `third_party/` first.

## Rules — code quality & performance (ponytail)

**Hard rule:** Prefer the smallest correct solution and protect performance. Applies to every agent session and code change — not only when the user says "ponytail".

Follow the ponytail ladder — stop at the first rung that holds:

1. **YAGNI** — does this need to exist?
2. **Reuse** — helper, util, or pattern already in this codebase?
3. **Stdlib / engine utilities** before bespoke code.
4. **Platform / RHI primitives** before new abstractions or dependencies.
5. **Existing dependencies** — never add a new one for what a few lines can do.
6. **One line** if one line is correct.
7. **Minimum diff** that fixes the real problem.

**Style:** No unrequested abstractions (single-impl interfaces, factories for one product). Deletion over addition; boring over clever; fewest files; shortest working diff — but only after tracing the full flow end to end. Bug fixes go at the shared choke point, not symptom patches in every caller. Mark deliberate simplifications with `// ponytail:` (name the ceiling and upgrade path).

**Performance:** Hot-path and render changes must not regress without explicit user approval. Use `Rules — performance testing` before claiming render/perf work is done. Profile before adding caches or speculative complexity — measure, then be lazy.

**Never ponytail away:** trust-boundary validation, data-loss prevention, security, accessibility, or anything the user explicitly requested in full.

Default engine posture is ponytail **full**. Use `/ponytail lite|ultra` when the user wants a different intensity.

## Rules — code layout (readable, not crammed)

**Hard rule:** the smallest correct change is measured in lines of logic, never in blank lines or braces. `clang-format` runs with `ColumnLimit: 0`, so it keeps whatever line breaks you write and will not un-cram code for you. Before writing, open the neighbouring hand-written file and match its density.

- **One statement per line.** No `if (x) return;`, no `int a = 1, b = 2;`, no braceless `for`/`if` that contains another control statement, no braceless body under a condition that spans lines.
- **Blank lines between steps.** Put one between logical steps of a function (gather / validate / compute / apply / return), after a group of guard clauses, and before and after any block you insert next to existing code. One blank line between functions, classes and declaration groups; never glue a new function to the closing `}` above it.
- **Wrap by operand.** A condition, argument list or string concatenation that no longer fits goes one operand or argument per line. Past three operands, name the sub-conditions as `const bool`. No line of code you write passes about 120 columns (comments, string literals and constant tables excepted).
- **Lambdas span lines.** `sol::overload` arms and multi-statement lambdas get a multi-line body; only one trivial expression may stay on one line.
- **Short functions.** A new function over about 50 lines, or with three or more `// ---` phases, is split into named helpers in the same change.
- **Same in every language.** Shaders (`.hlsl`, `.slang`), CMake and Lua follow the same rules; `constexpr` tables may stay dense.
- **Layout is not extra code.** It never justifies new abstractions, wrappers or comments (ponytail still applies); it only decides how the code you needed is written out.

Bad (real example from a past change) and good:

```cpp
if (meshIdx < 0) return;
Mesh &mesh = s->GetMesh(meshIdx);
if (!mesh.material) return;
MaterialInstance *inst = ensureInstance(s, mesh);
if (!inst) return;
inst->SetParam(name, v);
MirrorOpenPBRToLegacy(*inst);
```

```cpp
if (meshIdx < 0)
    return;

Mesh &mesh = s->GetMesh(meshIdx);
if (!mesh.material)
    return;

MaterialInstance *inst = ensureInstance(s, mesh);
if (!inst)
    return;

inst->SetParam(name, v);
MirrorOpenPBRToLegacy(*inst);
```

## Rules — tools

- **Use Grep / Glob for code lookups.** Use live source and focused wiki excerpts.

## Rules — performance testing

- **Scene:** `scene.load("Scenes/sponza.pescene")`. Do NOT use `load_model_async` — the saved scene has the correct camera setup.
- **Build:** Release for comparisons (Debug optimization is meaningless); Debug only for crash investigation. `tools/perf_cycle.sh Release` to kill, build, relaunch.
- **Present mode:** `rhi.change_present_mode("immediate")`, then verify via `engine.get_metrics().fps`. ~60 / vsync-locked = still FIFO; re-call.
- **Screenshot before changes** for a visual fidelity reference.
- **10 snapshots, 1 second apart** (not 0.5s — frame spikes inflate variance at 0.5s).
- **Compare:** `python3 tools/compare_snapshots.py baseline/ current/`. Baseline is fixed; only `current/` changes.
- **Regression thresholds:** FPS >5% drop AND >1 fps; ms >5% AND >0.5ms; VRAM >50 MB.

## Rules — RHI multi-backend carve smokes (Vulkan side, `DX12` branch)

When verifying RHI carve work (Phase 0/1), the `--api vulkan` smoke must load `sponza.pescene` before measuring liveness. Empty-editor smokes hide regressions in Image/Swapchain seams that only manifest once meshes/materials/shaders flow through the carved seam.

```powershell
echo '{"last_scene":"Assets/Scenes/sponza.pescene"}' > build-ninja-full\Release\Assets\editor_config.json
Remove-Item -Force -ErrorAction SilentlyContinue build-ninja-full\Release\PhasmaEditorModule_*.dll
$proc = Start-Process build-ninja-full\Release\PhasmaEditor.exe -ArgumentList '--api','vulkan' -WorkingDirectory build-ninja-full\Release -PassThru
Start-Sleep 18  # 12s is too short — sponza load + first-frame compile takes ~14s
if ($proc.HasExited) { "EXITED EARLY: code=0x{0:X8}" -f $proc.ExitCode } else { "ALIVE pid=$($proc.Id) after 18s"; Stop-Process -Id $proc.Id -Force }
Get-Content build-ninja-full\Release\PhasmaEngine.log -Tail 8
```

Tail must show `Scene loaded from: Assets/Scenes/sponza.pescene` and a Gbuffer shader Init line. The DX12-side smoke is unaffected (early `PE_ERROR` before scene load) but use the same `editor_config.json` on disk so build-tree state matches.

## Rules — PhasmaWebGPU

- **Read the W3C spec first** before any implementation. Navigate: grep `docs/webgpu-spec-graph.txt` → read section in `docs/webgpu-spec-sections.md` → check `docs/webgpu-spec-validation.md` for validation blocks → check `docs/webgpu-spec-musts.txt` for "must" constraints → read the algorithm at those line numbers in `build/_wgpu_spec/spec.txt` (source of truth, 52K lines). Implementation from header comments or memory produces silent spec violations. For every dictionary member referenced by the spec, honor it — silent ignore is a bug.
- **webgpu-samples ports are 1:1 with upstream.** Render-side must match upstream exactly: all models, all scenes, all procedural textures. Simplification is acceptable only for UI/interactive toggles, never for scene content. Procedural assets (canvas-drawn checkerboards, color grids, stripes) get ported to CPU code — do not replace them with stand-in images. Model loading code (`meshes/teapot.ts`, `primitives.ts`) gets ported as constexpr headers.

## Rules — DX12

- **API conservatism.** When adding multi-backend abstractions, expose only what must be backend-agnostic. Everything else stays private to its backend implementation. The public RHI surface is the contract; the cost of adding to it is high, the cost of leaving it private is low.

### Workspace scope: .

Imported from: INSTRUCTIONS.md

# PhasmaEngine — Instructions

The scoped Core and Editor sections below apply on top of the root rules; do not create separate instruction files.

## Where to find what

- **Code is the source of truth.** Derive from the actual files — do not paste these into docs:
  - Build commands → `CMakeLists.txt`
  - CMake targets, options, dependencies → `CMakeLists.txt` (root + per-subdirectory)
  - Boot sequence → `Phasma/Editor/Code/App/App.cpp`
  - Active render passes + `AddPass` signature → `Phasma/Editor/Code/Systems/RendererSystem.cpp` (`BuildRenderGraph`)
  - Singletons (`RHII`, `Context`, `EventSystem`) → headers under `Phasma/Core/Code/`
  - GPU material layout → `Phasma/Runtime/RuntimeAssets/Shaders/Common/Structures.hlsl`
  - Physics timestep / API → `Phasma/Runtime/Code/Systems/PhysicsSystem.cpp`
  - PE_API export sites → `grep PE_API Phasma/Core/Code/`

- **Code quality & performance (hard rule)** → `PhasmaHarness.md` (`Rules — code quality & performance (ponytail)`). Smallest correct diff, ponytail ladder, perf regression gates for hot paths. Default posture is ponytail **full** on every change.

- **Decisions, architecture, pitfalls and project history** → `<engine-wiki>/index.md`. Search focused pages; verify current behavior in source.

## Project knowledge — wiki only

Use the project's `<engine-wiki>/index.md` as the single maintained project knowledge entry point. Search live source for implementation; consult focused wiki pages for decisions, architecture and pitfalls. Verify historical claims against current source. Record durable discoveries in the relevant wiki page with source references and a verification date; do not create routine session diaries.

MemPalace and Graphify are retired from the active workflow. Do not query, update, rebuild or recreate their stores or invoke their hooks. Historical archives are for explicit recovery only. Keep personal preferences in global instructions, not a second project memory database. Do not automatically read entire wiki pages or maintenance logs when a focused search is enough.

## Wiki maintenance

Before finishing a session that changed code a wiki page describes, update the page and run `bash "<engine-wiki>/tools/lint.sh"`. Update `<engine-wiki>/index.md` and `<engine-wiki>/log.md` when pages are added or renamed.

## Multi-backend RHI (Vulkan + DX12)

Backend selection is resolved by `Phasma/Core/Code/API/GraphicsApiSelection.*`: explicit CLI `--api {vulkan,dx12}` wins, then `PHASMA_API`, then optional `phasma_settings.json` next to the executable (`graphics_api` or `api`), then the built-in Vulkan default. Invalid or unsupported CLI/env values hard-fail; unsupported persisted config values warn and fall back to Vulkan. DX12 is **Windows-only**; on Linux only Vulkan is supported.

### DX12 validation knobs (env vars, parsed in `Dx12RhiImpl::Init`)

| Var | Effect |
|---|---|
| `PE_DX12_DEBUG=1` / `0` | Enable / suppress the D3D12 debug layer and throttled warning/error info-queue callback. Defaults on in non-Release builds, off in Release. |
| `PE_DX12_GBV=1` | Enable GPU-Based Validation. Slow; catches resource-state mismatches. |
| `PE_DX12_BREAK=1` | Break on debug-layer `ERROR` / `CORRUPTION` severity (non-Release only). |
| `PE_DX12_DRED=1` / `0` | Enable / suppress DRED auto-breadcrumbs and page-fault tracking. Defaults on for `PE_DEBUG` / `PE_RELWITHDEBINFO`, off in Release. |

DRED auto-breadcrumbs + page-fault tracking default on for `PE_DEBUG` / `PE_RELWITHDEBINFO` builds.

### Backend-specific gaps and carve-outs

- **SSAO (FFX-CACAO)** — enabled on both backends as of `809c2aa5` (2026-05-08). DX12 routes through CACAO's D3D12 path with engine-owned compatibility inputs vendored under `Phasma/Runtime/third_party/CacaoCompat/` (DirectX-Headers v1.614.0 subset + locally-built DXIL + `UserMarker` stub). `SSAOPass` transitions the AO target to `UNORDERED_ACCESS` before CACAO's external draw, invalidates the shader-visible heap cache afterwards, and resyncs the engine state for `LightPass` sampling. Landing drawer: `phasmaengine/dx12-handoff/2026-05-08-dx12-ssao-landed-cacaocompat-relocated`.
- **ImGui platform windows** — enabled on DX12/Windows as of 2026-05-14. `GUIBackend::SupportsPlatformWindows()` advertises DX12 support only on `PE_WIN32`; the SDL2 + ImGui DX12 backends own secondary viewport creation and rendering.
- **Ray tracing** — DX12 advertises `caps.rayTracing = SupportsDxr()` on capable devices. DXR acceleration structures, ray-tracing pipelines and `TraceRays` are implemented; unsupported devices retain raster rendering. See `Dx12RhiImpl.cpp`, `Dx12AccelerationStructure.cpp`, `Dx12PipelineImpl.cpp` and `Dx12CommandBufferImpl.cpp` under `Phasma/Core/Code/API/DX12/`. Source verified 2026-10-07.
- **`CommandBuffer::PushDescriptor` / `SetEvent`** — `PE_ERROR` carve-outs on DX12. Audited 2026-05-06: zero callers tree-wide (PushDescriptor) / no Lua script invokes the binding (SetEvent). Implement when a real caller arrives.

### Verification recipe (per task gate)

```powershell
echo '{"last_scene":"Assets/Scenes/sponza.pescene"}' > build-ninja-full\Release\Assets\editor_config.json
Remove-Item -Force -ErrorAction SilentlyContinue build-ninja-full\Release\PhasmaEditorModule_*.dll
foreach ($api in 'vulkan','dx12') {
    $proc = Start-Process build-ninja-full\Release\PhasmaEditor.exe -ArgumentList '--api',$api -WorkingDirectory build-ninja-full\Release -PassThru
    Start-Sleep 18
    if ($proc.HasExited) { "$api EXITED EARLY: code=0x{0:X8}" -f $proc.ExitCode } else { "$api ALIVE pid=$($proc.Id) after 18s"; Stop-Process -Id $proc.Id -Force }
}
```

Empty-editor smokes hide regressions in Image / Swapchain seams. Always smoke with `sponza.pescene` loaded.

### Workspace scope: Phasma/Core

Imported from: Phasma/Core/INSTRUCTIONS.md

# PhasmaCore — Instructions

Supplements the root `PhasmaHarness.md`. PhasmaCore is the **shared library** (`PhasmaCore.dll`) — Vulkan RHI, ECS, base utilities. Editor UI, scene-asset tooling, and host-specific logic do not belong here.

For Vulkan resource creation, ECS access patterns, boot sequence — read the actual headers under `Code/API/`, `Code/ECS/`, `Code/Base/`. They're the source of truth and don't rot.

## Rules specific to PhasmaCore

- **No ImGui** includes or calls — PhasmaCore is platform-agnostic.
- **No editor-specific logic** — that belongs in host/editor layers.
- **No implicit asset roots or sibling-project assumptions** — hosts prepare/set runtime assets; core APIs take explicit paths, memory, or bytecode.
- **All GPU resource creation goes through the wrappers** in `Code/API/` (`Buffer`, `Image`, `Sampler`) — never raw `VkBuffer` / `VkImage` / `VkDeviceMemory`.

### Workspace scope: Phasma/Editor

Imported from: Phasma/Editor/INSTRUCTIONS.md

# PhasmaEditor — Instructions

Supplements the root `PhasmaHarness.md`. PhasmaEditor is the desktop editor executable — links PhasmaCore, Assimp, MeshOptimizer, Jolt, ImGui, Lua. All editor code lives in `PhasmaEditorModule.dll` (hot-reloaded); `PhasmaEditor.exe` is the editor host, while `PhasmaLauncher` is the standalone target/backend/sample selector.

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
