# PhasmaEngine Wiki Log

## 2026-05-15

- Upgraded raster shadow quality controls: cascades now use camera-forward view depth, configurable shadow distance/split lambda, texel-snapped light projections, per-cascade world texel sizes, angle-aware receiver normal bias, filter radius, and last-cascade fade; shadow depth bias is enabled for Vulkan and baked into DX12 PSOs, with editor debug modes for cascade bands and raw shadow factor.
- Added DX12 material struct reflection for editor material layouts: the DX12 HLSL source-binding parser now records flat builtin struct-member offsets/sizes for `StructuredBuffer<T>` element structs, and `MaterialReflectionBackend` uses that path so `standard_pbr` can expose `MaterialGpuData` scalar/color fields on DX12. Texture-slot reflection remains Vulkan/SPIR-V-only for this slice.

## 2026-05-14

- Enabled DX12/Windows ImGui platform-window capability in `GUIBackend::SupportsPlatformWindows`; SDL2 and ImGui's DX12 renderer now handle secondary viewport creation and rendering for DX12.
- Verified the DX12 ImGui platform-window path with Sponza loaded: a temporary floating `Models` viewport produced a secondary HWND and rendered ImGui contents without crashing.
- Fixed and verified DX12 AABB rendering. The AABB shader color varying now uses `TEXCOORD0` before `SV_POSITION`, matching DX12 stage-linkage register expectations; Sponza with `draw_aabbs=true` stays alive and renders depth-aware 1px AABB lines.
- Reworked DX12 color `BlitImage` scaling around a DX12-private `Dx12Blit` helper called from `Dx12ImageImpl::Blit` after the normal transfer barriers. Same-size blits use `CopyTextureRegion`; scaled same-format 2D color blits use a private PSO with PhasmaCore-owned embedded DXIL bytecode. Depth/stencil, format conversion, MSAA, arrays, 3D scaling, and integer typed shader variants remain explicit future slices.
- Moved the core Downsampler off runtime editor shader assets by embedding precompiled SPIR-V and DXIL bytecode in PhasmaCore. The regeneration HLSL source and FFX SPD headers now live beside the Downsampler, while DX12 direct-bytecode reflection uses a small binding metadata string.
- Removed PhasmaCore's dev-tree fallback to `PhasmaEditor/Assets`; the core path helper now only resolves executable-local `Assets/`, with hosts/builds responsible for preparing that runtime root.
- Removed the hidden editor bootstrap dependency from source shader creation: `Shader::Create` now verifies the source file directly and registers its own core `FileWatcher` callback when the host has not already done so.
- Made the runtime asset-copy stamp configuration-specific so Debug/Release builds each prepare their own executable-local `Assets/` tree instead of sharing one top-level stamp.
- Tightened PhasmaCore's exported build surface by making its precompiled header private and keeping DX12 system libraries as PhasmaCore-private link dependencies; the editor module now links its DX12 GUI/CACAO dependencies explicitly.
- Removed the editor window resize path's direct `SDL_vulkan.h` dependency by using SDL's backend-neutral pixel-size query for drawable extents.
- Narrowed PhasmaCore's public third-party build surface: core now publishes only the neutral public include roots its public headers need, keeps its third-party library search paths/link libraries private, and makes consumers explicitly include/link shader compilers, SPIRV-Cross, RenderDoc, stb, VMA, Vulkan, RapidJSON, or SDL when they use them directly.
- Removed the stale LightPass `tonemapping/fsr2` UBO plumbing and DX12 gate helper. `fsr2` had been fed from `taa`, no `GlobalSettings::fsr2` exists, and `LightingPS.hlsl` did not consume either flag; real postprocess behavior remains driven by explicit render-graph passes (`TAA`, `Sharpen`, `Upsample`, `Tonemap`, etc.).
- Enabled the first DX12 ray-tracing path behind the normal `rayTracing` cap: DX12 now creates BLAS/TLAS resources, writes AS descriptors, compiles DXR libraries, creates state objects/SBTs, and dispatches rays. Sponza full RT on DX12 was smoke-tested with the debug layer after fixing AS resource states, DXR push-constant/register rewriting, SBT raygen record sizing, recursion depth, and the RT instance-to-mesh-constants index.
- Fixed the first DX12 RT visual routing issues: RT-only mode now makes the RT `viewport` image become a fresh `display` image before Grid/GUI/final blit, avoiding recursive viewport capture; DX12 Hybrid now matches Vulkan by disabling `GBufferTransparent` outside Raster mode so transparent geometry is handled by the RT pass instead of being pre-rastered.
- Matched DX12 RT-only TAA routing to the Vulkan shape: when TAA is enabled, DX12 keeps the depth/GBuffer velocity path alive and runs TAA/Sharpen over the RT `viewport` instead of applying camera jitter and then doing only a plain upsample. Render-mode changes now reset TAA history.

## 2026-05-12

- Restored the minimal wiki scaffold and tools referenced by root agent instructions.
- MemPalace remains the primary long-form history and handoff store; this wiki is the scan-friendly synthesis layer.
