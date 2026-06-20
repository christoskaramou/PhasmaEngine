# Occlusion Culling + Cross-API Occlusion Queries

**Date:** 2026-06-20
**Status:** Plan (no code changed)
**Authors:** Claude (Opus 4.8) + Codex (gpt-5.5) — independent investigations that converged on the same recommendation.

---

## Goal

Add occlusion support that works across **all** RHI backends (Vulkan + DX12, plus the WebGPU C-API layer over Vulkan). "Occlusion queries that work for all APIs" has two legitimate readings, and we deliver both as separate layers:

- **Layer A — Neutral hardware occlusion-query RHI surface** (the literal ask). Promote the existing **Vulkan-only** WebGPU occlusion-query code into a backend-agnostic `QueryPool`, modeled on the existing cross-backend `GpuTimer`. Makes `VK_QUERY_TYPE_OCCLUSION` / `D3D12_QUERY_TYPE_OCCLUSION` available on both backends. For tooling / diagnostics / CTS coverage — **not** the main scene cull.
- **Layer B — GPU-driven Hi-Z occlusion culling** (the real perf win). The recommended renderer path. Extends the GPU-driven frustum culling the engine already has rather than adding a new mechanism.

The two layers are independent; ship Layer A first (it is self-contained and low-risk), then Layer B.

---

## Technique decision

| Technique | Verdict | Reason |
|---|---|---|
| **Hi-Z compute cull** | ✅ Primary (Layer B) | Pure compute + sampled image + the existing indirect buffers → **identical on Vulkan & DX12, zero extension dependencies, no CPU stall**. Slots into `CullingCS.hlsl` beside the frustum test. |
| **HW occlusion queries** (`VK_QUERY_TYPE_OCCLUSION` / `D3D12_QUERY_TYPE_OCCLUSION`) | ⚠️ Secondary (Layer A) | Per-object CPU readback = sync stall or multi-frame latency; does not feed the compacted indirect buffers without an extra GPU compaction pass. Keep for tooling / WebGPU coverage. |
| **Predication / conditional rendering** (`VK_EXT_conditional_rendering` / `ID3D12GraphicsCommandList::SetPredication`) | ❌ Not for the renderer | Vulkan side is extension-gated; the VK predicate-buffer model does not map cleanly to DX12's 64-bit predication semantics; per-object predication breaks indirect batching. Optional Layer-A extra at most. |

**Why Hi-Z is the architectural fit:** the engine already has the two prerequisites —

1. GPU-driven frustum culling that compacts visible draws into per-material-type indirect buffers + a `Counters[7]` buffer — `Phasma/Runtime/RuntimeAssets/Shaders/Compute/CullingCS.hlsl`, driven by `Scene::DispatchCulling` (`Phasma/Runtime/Code/Scene/Scene.cpp:897`).
2. A reverse-Z depth prepass producing `depthStencil` — `Phasma/Runtime/Code/RenderPasses/DepthPass.cpp`.

Hi-Z reuses both: build a depth pyramid from the prepass, add a screen-space AABB test to the existing cull shader. No new draw-submission mechanism, no extensions, no readback.

---

## Layer A — Neutral QueryPool (cross-API hardware queries)

### Surface

Model the factory dispatch on `Phasma/Core/Code/API/GpuTimerBackend.cpp` (dispatch by `RHII.GetApi()`), and the per-backend impl on `VulkanGpuTimerImpl` / `Dx12GpuTimerImpl`.

`Phasma/Core/Code/API/RHITypes.h` — new enums:

```cpp
enum PeQueryType        { PE_QUERY_TYPE_OCCLUSION, PE_QUERY_TYPE_TIMESTAMP };
enum PeQueryControlFlags{ PE_QUERY_CONTROL_NONE = 0, PE_QUERY_CONTROL_PRECISE = 1 << 0 };
enum PeQueryResultFlags { PE_QUERY_RESULT_64_BIT = 1<<0, PE_QUERY_RESULT_WAIT = 1<<1, PE_QUERY_RESULT_WITH_AVAILABILITY = 1<<2 };
```

`Phasma/Core/Code/API/RHI.h` — extend `GpuFeatureSupport`:

```cpp
bool occlusionQuery        = false;
bool preciseOcclusionQuery = false;
bool binaryOcclusionQuery  = false;  // DX12 only; VK has no binary occlusion type
bool queryPoolHostReset    = false;  // VK hostQueryReset; do NOT assume it
bool conditionalRendering  = false;  // VK_EXT_conditional_rendering
```

New neutral class `QueryPool` (`Phasma/Core/Code/API/QueryPool.{h,cpp}`) with a `QueryPoolDesc { PeQueryType type; uint32_t count; bool binary; std::string name; }`.

`CommandBuffer` additions (`Command.h` + `CommandBuffer_Internal.h` virtual + both backend impls):

```cpp
void ResetQueryPool(QueryPool*, uint32_t first, uint32_t count);
void BeginQuery(QueryPool*, uint32_t index, PeQueryControlFlags = PE_QUERY_CONTROL_NONE);
void EndQuery(QueryPool*, uint32_t index);
void ResolveQueryPool(QueryPool*, uint32_t first, uint32_t count, Buffer* dst, uint64_t dstOffset,
                      uint64_t stride = sizeof(uint64_t), PeQueryResultFlags = PE_QUERY_RESULT_64_BIT);
```

Optional predication (gate on `conditionalRendering`, Layer-A-only, do not let the renderer depend on it):

```cpp
void BeginConditionalRendering(Buffer* predicate, uint64_t offset, bool inverted);
void EndConditionalRendering();
```

### Backend cross-walk

**Vulkan:**
- Create/destroy: `vkCreateQueryPool` (`VkQueryPoolCreateInfo::queryType = VK_QUERY_TYPE_OCCLUSION | VK_QUERY_TYPE_TIMESTAMP`) / `vkDestroyQueryPool`.
- Reset: prefer `vkCmdResetQueryPool`; host reset (`vkResetQueryPool`) **only** if `queryPoolHostReset`. Never reset inside a render pass / dynamic rendering. **Do not copy `VulkanGpuTimerImpl`'s unconditional host-reset assumption** into the generic pool.
- Begin/end: `vkCmdBeginQuery` (+`VK_QUERY_CONTROL_PRECISE_BIT` only if `preciseOcclusionQuery`) / `vkCmdEndQuery`.
- Resolve: `vkCmdCopyQueryPoolResults` with `VK_QUERY_RESULT_64_BIT`; `WAIT_BIT` for blocking/debug only; `WITH_AVAILABILITY_BIT` for async polling.
- Conditional rendering: require `VK_EXT_conditional_rendering`, enable `VkPhysicalDeviceConditionalRenderingFeaturesEXT::conditionalRendering`, `vkCmdBegin/EndConditionalRenderingEXT`, add `VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT` usage/state mapping.

**DX12:**
- Create: `ID3D12Device::CreateQueryHeap` (`D3D12_QUERY_HEAP_TYPE_OCCLUSION | _TIMESTAMP`).
- Types: `D3D12_QUERY_TYPE_OCCLUSION`, `_BINARY_OCCLUSION`, `_TIMESTAMP`.
- Reset: **DX12 has no query reset** → neutral `ResetQueryPool` is a no-op + engine-side availability bookkeeping.
- Begin/end: `ID3D12GraphicsCommandList::BeginQuery` / `EndQuery`.
- Resolve: `ResolveQueryData` into a readback/GPU buffer after the right state transition.
- Predication: `SetPredication(res, offset, D3D12_PREDICATION_OP_NOT_EQUAL_ZERO)`; end with `SetPredication(nullptr, 0, ...)`; offsets must be 64-bit-aligned; add `D3D12_RESOURCE_STATE_PREDICATION`.

### Mappings to explicitly gate (not clean across backends)
- VK precise occlusion is feature-gated; VK has no distinct *binary* occlusion type (DX12 does).
- VK conditional rendering is extension-gated; DX12 predication is core but semantically different.
- VK query-reset location matters; DX12 has no reset.
- CPU readback must be fenced and async — never block the render loop.

### WebGPU promotion
Rewire `Phasma/WebGPU/Code/QueryCommands.{h,cpp}` (+ `QuerySet.h`, `RenderPass.cpp`, `CommandEncoder.cpp`) onto the neutral `QueryPool`. Preserve: `beganIndices` validation; "reset the query pool before beginning rendering, not inside the render pass" rule from `RenderPass.cpp`; resolve-unbegun-slots-to-zero behavior.

---

## Layer B — Hi-Z occlusion culling (renderer)

### Render-graph integration

Scene passes are registered on a **numeric-priority** render graph via `m_sceneRenderer.AddScenePassesToRenderGraph()` in `Phasma/Runtime/Code/Render/SceneRenderGraph.cpp` (the GUI pass sits at priority 10000, added in `RendererSystem::BuildRenderGraph()`). Insert two new passes **between DepthPass and GBuffer**:

```
Culling (frustum-only)  →  Shadow  →  DepthPass (reverse-Z)
   →  [NEW] DepthPyramidPass   →   [NEW] OcclusionCullingPass (frustum + Hi-Z)
   →  GBuffer / transparents / light   (consume the re-culled buffers)
```

> **Confirm the exact priority integers** in `AddScenePassesToRenderGraph()` at implementation time. Do not hardcode assumed numbers (50/100/200/300) — read the source and slot between the real Depth and GBuffer priorities.

### Design choice: current-frame Hi-Z, single phase

Correct, with no temporal artifacts:

1. Prepass draws the **full frustum-visible** opaque+alpha-cut set → `depthStencil`.
2. `DepthPyramidPass` builds an R32F mip pyramid from `depthStencil`.
3. `OcclusionCullingPass` re-runs `DispatchCulling` in `FrustumAndHiZ` mode, **overwriting the same indirect + counter buffers** (with a buffer barrier before GBuffer consumes them).
4. GBuffer / transparent / transmission / light passes consume the occlusion-culled buffers.

**What this saves:** GBuffer + later-pass cost. **What it does NOT save:** the depth prepass itself — that requires *previous-frame* Hi-Z (two-phase), which introduces temporal popping and is deferred to milestone 5.

**Correctness:** an object can never occlude itself (its own depth is included in the tile-min), and anything culled from GBuffer is provably behind prepass geometry, so no holes.

### Reverse-Z Hi-Z math (the easiest thing to invert)

- Pyramid: a **separate** `PE_FORMAT_R32_SFLOAT` image with a full mip chain, sampled + storage usage. Do **not** try to mip the depth-stencil image itself. Reuse the mip-target helper pattern in `Phasma/Runtime/Code/Render/SceneRenderTargets.cpp`.
- Reduction: reverse-Z → store **minimum** depth per texel; out-of-bounds children use the far value `0.0`. (Normal-Z would be max / `1.0`.)
- Per-object test:
  - `objectNearestDepth = max(projected AABB corner depths)`  (reverse-Z: nearest = largest)
  - `tileDepth = min(4 mip samples at chosen mip)`
  - **cull if `objectNearestDepth <= tileDepth - bias`**
- Mip select: `clamp(floor(log2(max(rectWidthPx, rectHeightPx))), 0, mipCount - 1)`.
- Conservative keep-visible rules (favor false-negatives): object intersects near plane; screen rect partly outside viewport; rect extremely small (until validated); any object class with unstable bounds. Default `bias = 1e-4`.

### Preserve the counter / indirect contract — DO NOT REORDER

`Counters[7]` layout shared by depth, gbuffer, transparency sort, **and** the object-id perception path:

```
[0] opaqueSS  [1] alphaCutSS  [2] alphaBlend  [3] transmission  [4] selected  [5] opaqueDS  [6] alphaCutDS
```

- `DepthPass.cpp` keeps offsets `0, 1, 5, 6` (`DepthPass.cpp:128-133`).
- `GbufferPass.cpp` keeps offsets `0, 1, 2, 3` (`GbufferPass.cpp:176-181`).
- The Hi-Z cull adds a test **before** the existing indirect writes / counter increments — **no new output buffers**.

Extend `Scene::DispatchCulling(cmd, passInfo, sortPassInfo)` with a culling mode + pyramid image:

```cpp
enum class CullingMode { FrustumOnly, FrustumAndHiZ };
```

**Push-constant note:** `CullingCS.hlsl`'s push-constant block is already 128 B (frustum planes). Do **not** add a matrix there — pass the camera view-projection via the existing `NodeData` byte-address buffer (as `DepthVS.hlsl` already does), and fit only small flags/bias into existing padding (or a small dedicated constant buffer).

---

## File-by-file change list

### Layer A — neutral query API
New: `Phasma/Core/Code/API/QueryPool.{h,cpp}`, `Phasma/Core/Code/API/Vulkan/VulkanQueryPool.{h,cpp}`, `Phasma/Core/Code/API/DX12/Dx12QueryPool.{h,cpp}`.
Edit: `RHITypes.h`, `RHI.{h,cpp}`, `Command.{h,cpp}`, `CommandBuffer_Internal.h`, `Vulkan/VulkanCommandBufferImpl.{h,cpp}`, `DX12/Dx12CommandBufferImpl.{h,cpp}`.
Optional predication: `Vulkan/VulkanRHITypeUtils.*`, `Vulkan/VulkanBufferImpl.cpp`, `DX12/Dx12Translate.h`, `DX12/Dx12BufferImpl.cpp`.
WebGPU promotion: `WebGPU/Code/QuerySet.h`, `QueryCommands.{h,cpp}`, `RenderPass.cpp`, `CommandEncoder.cpp`.

### Layer B — Hi-Z culling
New: `Phasma/Runtime/Code/RenderPasses/DepthPyramidPass.{h,cpp}`, `Phasma/Runtime/RuntimeAssets/Shaders/Compute/DepthPyramidCS.hlsl`.
Edit: `Shaders/Compute/CullingCS.hlsl`, `Scene/Scene.{h,cpp}` (`DispatchCulling` signature), `RenderPasses/CullingPass.{h,cpp}`, `Render/SceneRenderGraph.{h,cpp}`, `Render/SceneRenderTargets.{h,cpp}` (R32F mip target), and the two consumers `RenderPasses/DepthPass.cpp` / `RenderPasses/GbufferPass.cpp` (only if the second-cull wiring requires it).

### Settings / UI
`Core/Code/Base/Settings.h`, `Core/Code/Base/SettingsBindings.cpp`, `Runtime/Code/Scene/SceneSerializer.cpp`, `Editor/Code/GUI/Widgets/GlobalWidget.cpp`:

```cpp
bool  occlusion_culling      = false;   // default OFF until both backends validated
float occlusion_culling_bias = 0.0001f;
```

Checkbox next to the existing frustum-culling control.

### Docs (after implementation)
Update the relevant `docs/wiki/` page and run `bash docs/wiki/tools/lint.sh`.

---

## Phasing (each milestone independently shippable + verifiable on both backends)

1. **Neutral QueryPool (Layer A).** Implement `QueryPool` + command-buffer query methods for both backends, populate `GpuFeatureSupport`, rewire WebGPU onto it. Renderer behavior unchanged.
   *Verify:* both APIs build; WebGPU occlusion query path runs on DX12; no frame-behavior change.
2. **DepthPyramidPass.** Build R32F mips from `depthStencil`; not yet used for culling.
   *Verify:* sponza on `--api vulkan` and `--api dx12`; inspect pyramid in RenderDoc/PIX; zero visual change.
3. **Second cull pass, occlusion OFF.** Add `OcclusionCullingPass` after the pyramid in frustum-only mode to validate graph order, barriers, descriptor wiring, counter reuse.
   *Verify:* `DepthPass` still consumes `0,1,5,6` and `GbufferPass` `0,1,2,3`; output matches baseline.
4. **Enable Hi-Z test in `CullingCS.hlsl`,** gated by `settings.occlusion_culling`.
   *Verify:* start OFF; enable on Vulkan then DX12; screenshot diff; cross-check visible set against the existing object-id perception path; watch screen edges / near plane / large occluders for false-positive culling.
5. **Harden / optimize.** Optional previous-frame pyramid to also cull the depth prepass, with temporal hysteresis ("visible last frame" protection) if popping appears.

### Per-backend verification harness
- `tools/perf_cycle.sh Release` (Release only for perf comparisons).
- `scene.load("Scenes/sponza.pescene")` (do not use `load_model_async`).
- `rhi.change_present_mode("immediate")`, verify via `engine.get_metrics().fps`.
- Screenshot before changes; 10 snapshots, 1 s apart; `python3 tools/compare_snapshots.py baseline/ current/`.
- Launch each backend: `build-ninja-full/Release/PhasmaEditor.exe --api vulkan` / `--api dx12`.
- Regression gates: FPS >5% **and** >1 fps; ms >5% **and** >0.5 ms; VRAM >50 MB.

---

## Risks & gotchas

- **Reverse-Z inversion** — min-reduce the pyramid, compare object-nearest (`max`) `<=` tile-min with a small bias. Most likely bug; verify visually with the setting toggled.
- **False-positive culling** is the main correctness risk — prefer conservative visibility over aggressive culling (near plane, off-screen rects, tiny rects all keep-visible).
- **Occluder source** — the depth prepass must not treat alpha-blend / transmission as occluders; opaque + alpha-cut depth is the correct source.
- **Current-frame Hi-Z saves GBuffer cost, not prepass cost.** Previous-frame Hi-Z can cull the prepass too but adds temporal artifacts — keep it a later milestone.
- **Counter / indirect layout is a shared contract** (depth, gbuffer, sort, and the object-id perception path). Never reorder the 7-slot layout or the indirect-buffer ordering.
- **Capability-gate everything backend-specific:** VK host query reset is not guaranteed; DX12 has no query reset; VK conditional rendering is optional and unlike DX12 predication. The renderer path must never depend on conditional rendering.
- **Default the feature OFF** in settings until both backends are validated.

---

## Provenance

Investigated by Claude (Opus 4.8, in-editor) and Codex (gpt-5.5, `--sandbox read-only`, xhigh reasoning) independently; both converged on Hi-Z compute culling as the renderer path with a neutral hardware-query surface as the cross-API layer. Codex's full unabridged write-up was captured during the session (background task `ba85n5dsl`).
