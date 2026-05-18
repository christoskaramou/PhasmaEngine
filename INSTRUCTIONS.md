# PhasmaEngine — Instructions

Subdirectory `INSTRUCTIONS.md` files cascade on top of this root.

## Where to find what

- **Code is the source of truth.** Derive from the actual files — do not paste these into docs:
  - Build commands → `CMakeLists.txt`
  - CMake targets, options, dependencies → `CMakeLists.txt` (root + per-subdirectory)
  - Boot sequence → `PhasmaEditor/Code/App/App.cpp`
  - Active render passes + `AddPass` signature → `PhasmaEditor/Code/Systems/RendererSystem.cpp` (`BuildRenderGraph`)
  - Singletons (`RHII`, `Context`, `EventSystem`) → headers under `PhasmaCore/Code/`
  - GPU material layout → `PhasmaEditor/Assets/Shaders/Common/Structures.hlsl`
  - Physics timestep / API → `PhasmaEditor/Code/Systems/PhysicsSystem.cpp`
  - PE_API export sites → `grep PE_API PhasmaCore/Code/`

- **Non-obvious rules and gotchas live in MemPalace.** Search with `mempalace_search` for things like: PCH `<vector>` trap, GPU resource wrappers (`Buffer`/`Image`/`Sampler`), perf regression thresholds, hot-reload safe-window / ImGui forwarding, scripting MCP caveat (`ScriptSystem*` caching), `MaterialGpuData` 80-byte layout + IOR/transmission flush rule. Use lowercase `phasmaengine` for new project memories.

- **Recent state and session history** → `mempalace_kg_timeline`, `mempalace_diary_read`.

- **Synthesis pages** (architecture overviews, testing playbooks, pitfalls) → `docs/wiki/index.md`. Find with `bash docs/wiki/tools/search.sh "query"`.

## MemPalace workflow

- Start architecture, handoff, or project-history work with `mempalace_status`; use CLI `mempalace wake-up --wing phasmaengine` when available, then targeted `mempalace_search` / `mempalace_kg_query`.
- Use `mempalace_add_drawer` for durable verbatim handoffs, decisions, and discoveries; use `mempalace_diary_write` at session end.
- Use `mempalace_kg_add` / `mempalace_kg_invalidate` for stable facts and superseded relationships instead of hiding them in prose-only drawers.
- Use taxonomy and graph tools (`mempalace_get_taxonomy`, `mempalace_graph_stats`, `mempalace_traverse`) when a task spans subsystems or wings.
- After large instruction, doc, or history imports, dry-run `mempalace mine`, avoid build/generated/vendor trees, then refresh compressed recall with `mempalace compress --wing phasmaengine`.

## Wiki maintenance

Before finishing a session that changed code a wiki page describes, update the page and run `bash docs/wiki/tools/lint.sh`. Update `docs/wiki/index.md` and `docs/wiki/log.md` when pages are added or renamed.

## Multi-backend RHI (Vulkan + DX12)

Backend selection is resolved by `PhasmaCore/Code/API/GraphicsApiSelection.*`: explicit CLI `--api {vulkan,dx12}` wins, then `PHASMA_API`, then optional `phasma_settings.json` next to the executable (`graphics_api` or `api`), then the built-in Vulkan default. Invalid or unsupported CLI/env values hard-fail; unsupported persisted config values warn and fall back to Vulkan. DX12 is **Windows-only**; on Linux only Vulkan is supported.

### DX12 validation knobs (env vars, parsed in `Dx12RhiImpl::Init`)

| Var | Effect |
|---|---|
| `PE_DX12_DEBUG=1` / `0` | Enable / suppress the D3D12 debug layer and throttled warning/error info-queue callback. Defaults on in non-Release builds, off in Release. |
| `PE_DX12_GBV=1` | Enable GPU-Based Validation. Slow; catches resource-state mismatches. |
| `PE_DX12_BREAK=1` | Break on debug-layer `ERROR` / `CORRUPTION` severity (non-Release only). |
| `PE_DX12_DRED=1` / `0` | Enable / suppress DRED auto-breadcrumbs and page-fault tracking. Defaults on for `PE_DEBUG` / `PE_RELWITHDEBINFO`, off in Release. |

DRED auto-breadcrumbs + page-fault tracking default on for `PE_DEBUG` / `PE_RELWITHDEBINFO` builds.

### Backend-specific gaps and carve-outs

- **SSAO (FFX-CACAO)** — enabled on both backends as of `809c2aa5` (2026-05-08). DX12 routes through CACAO's D3D12 path with engine-owned compatibility inputs vendored under `PhasmaRuntime/third_party/CacaoCompat/` (DirectX-Headers v1.614.0 subset + locally-built DXIL + `UserMarker` stub). `SSAOPass` transitions the AO target to `UNORDERED_ACCESS` before CACAO's external draw, invalidates the shader-visible heap cache afterwards, and resyncs the engine state for `LightPass` sampling. Landing drawer: `phasmaengine/dx12-handoff/2026-05-08-dx12-ssao-landed-cacaocompat-relocated`.
- **ImGui platform windows** — enabled on DX12/Windows as of 2026-05-14. `GUIBackend::SupportsPlatformWindows()` advertises DX12 support only on `PE_WIN32`; the SDL2 + ImGui DX12 backends own secondary viewport creation and rendering.
- **Ray tracing** — `caps.rayTracing == false` on DX12 by design; RT pass is skipped. DXR is a separate Phase.
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
