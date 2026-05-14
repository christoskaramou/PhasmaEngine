# PhasmaEngine Wiki Log

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
- Classified the LightPass `tonemapping/fsr2` UBO path as legacy flag plumbing. `fsr2` is fed from `taa`, no `GlobalSettings::fsr2` exists, and `Lighting.hlsl` declares but does not consume `cb_tonemapping` or `cb_fsr2`; real postprocess behavior is driven by explicit render-graph passes (`TAA`, `Sharpen`, `Upsample`, `Tonemap`, etc.).

## 2026-05-12

- Restored the minimal wiki scaffold and tools referenced by root agent instructions.
- MemPalace remains the primary long-form history and handoff store; this wiki is the scan-friendly synthesis layer.
