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

- **Non-obvious rules and gotchas live in MemPalace.** Search with `mempalace_search` for things like: PCH `<vector>` trap, GPU resource wrappers (`Buffer`/`Image`/`Sampler`), perf regression thresholds, hot-reload safe-window / ImGui forwarding, scripting MCP caveat (`ScriptSystem*` caching), `MaterialGpuData` 80-byte layout + IOR/transmission flush rule.

- **Recent state and session history** → `mempalace_kg_timeline`, `mempalace_diary_read`.

- **Synthesis pages** (architecture overviews, testing playbooks, pitfalls) → `docs/wiki/index.md`. Find with `bash docs/wiki/tools/search.sh "query"`.

## Wiki maintenance

Before finishing a session that changed code a wiki page describes, update the page and run `bash docs/wiki/tools/lint.sh`. Update `docs/wiki/index.md` and `docs/wiki/log.md` when pages are added or renamed.

## Multi-backend RHI (Vulkan + DX12)

Backend is selected at process launch via `--api {vulkan,dx12}` (default: vulkan). Parsed in `PhasmaEditor/main.cpp`. DX12 is **Windows-only**; on Linux only `--api vulkan` is supported.

### DX12 validation knobs (env vars, parsed in `Dx12RhiImpl::Init`)

| Var | Effect |
|---|---|
| `PE_DX12_DEBUG=1` | Enable the D3D12 debug layer (auto-on in non-Release builds). |
| `PE_DX12_GBV=1` | Enable GPU-Based Validation. Slow; catches resource-state mismatches. |
| `PE_DX12_BREAK=1` | Break on debug-layer `ERROR` / `CORRUPTION` severity (non-Release only). |

DRED auto-breadcrumbs + page-fault tracking are forced on for `PE_DEBUG` / `PE_RELWITHDEBINFO` builds.

### Backend-specific gaps and carve-outs

- **SSAO (FFX-CACAO)** — Vulkan-only today. CACAO's D3D12 `InitContext` triggers `DXGI_ERROR_DEVICE_REMOVED`; engine-side carve is correct but the upstream init path needs `ID3D12InfoQueue` instrumentation to diagnose. State + resume recipe in MemPalace drawer `phasmaengine/design/2026-05-05-rhi-phase1-t14d-ssao-PARKED-cacao-init-device-removed`.
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
