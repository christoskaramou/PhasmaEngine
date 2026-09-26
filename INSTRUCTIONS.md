# PhasmaEngine — Instructions

Subdirectory `INSTRUCTIONS.md` files cascade on top of this root.

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

- **Code quality & performance (hard rule)** → `AGENTS.md` (`Rules — code quality & performance (ponytail)`). Smallest correct diff, ponytail ladder, perf regression gates for hot paths. Default posture is ponytail **full** on every change.

- **Decisions, architecture, pitfalls and project history** → `docs/wiki/index.md`. Search focused pages; verify current behavior in source.

## Project knowledge — wiki only

Use the project's `docs/wiki/index.md` as the single maintained project knowledge entry point. Search live source for implementation; consult focused wiki pages for decisions, architecture and pitfalls. Verify historical claims against current source. Record durable discoveries in the relevant wiki page with source references and a verification date; do not create routine session diaries.

MemPalace and Graphify are retired from the active workflow. Do not query, update, rebuild or recreate their stores or invoke their hooks. Historical archives are for explicit recovery only. Keep personal preferences in global instructions, not a second project memory database. Do not automatically read entire wiki pages or maintenance logs when a focused search is enough.

## Wiki maintenance

Before finishing a session that changed code a wiki page describes, update the page and run `bash docs/wiki/tools/lint.sh`. Update `docs/wiki/index.md` and `docs/wiki/log.md` when pages are added or renamed.

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
