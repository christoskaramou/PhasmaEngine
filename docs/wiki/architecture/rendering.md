# Rendering And RHI Notes

The render path is split between PhasmaRuntime's shared scene renderer and PhasmaCore's backend-neutral RHI wrappers. Code remains the source of truth; use this page for cross-cutting pitfalls that are easy to miss when reading one file at a time.

## Forward+ Light Culling

Raster lighting uses `ForwardPlusLightCullingPass` before the opaque and transparent light passes when `GlobalSettings::forward_plus` is enabled. The compute pass bins point and spot lights into 16x16 screen-space tiles, writing per-tile counts and compact index lists that `LightingPS.hlsl` consumes instead of looping over every local light for every pixel. Directional and area lights remain on the existing full-list paths.

The culler intentionally uses a conservative projected sphere overlap for each local light. If a tile reaches `FORWARD_PLUS_MAX_LIGHTS_PER_TILE`, the tile records an overflow bit and the pixel shader falls back to the original full point or spot loop for that light class. This preserves correctness for extreme scenes while keeping common scenes on the cheaper tiled path.

Forward+ resources are descriptor-sensitive: scene render descriptor refresh must update the culling pass before `LightPass`, because the lighting descriptor set binds the cull buffers as extra read-only resources. When the global option is disabled or the raster path is inactive, `LightPass` binds small fallback storage buffers and the shader uses the original full point/spot loops. The pass also records explicit buffer barriers between compute writes and lighting reads on both Vulkan and DX12.

## Present Mode And Framebuffer Cache

Present-mode changes must be treated as resize work, not as an immediate mid-frame swapchain teardown. Editor/runtime requests should update the desired surface present mode, sync the stored preference to the effective supported mode, wait for idle, and queue `EventType::Resize` so swapchain, scene render targets, render-pass components, and frame resources rebuild together. Host window titles should be refreshed after the deferred swapchain recreation and should report the recreated swapchain's actual present mode.

Cached framebuffers are keyed by the actual attachment `ImageView` objects captured in the backend framebuffer. Implicit render-target views must be created before cache lookup so an RTV recreation cannot reuse a framebuffer that still holds a destroyed Vulkan image view or DX12 descriptor.

## Cached Pipelines

- `CommandBuffer::GetPipeline()` caches `Pipeline` objects globally from a `PassInfo` hash. Some callers, such as skybox HDR-to-cubemap conversion, build transient `PassInfo` instances and destroy them after the command buffer finishes.
- A cached `Pipeline` must treat the source `PassInfo` as construction-only state. Runtime command recording should use immutable metadata copied into `Pipeline` at creation time, such as pipeline type and push-constant ranges, instead of reading through the original `PassInfo` reference.
- Vulkan descriptor auto-binding must still run when rebinding resources for the same cached pipeline. Skipping descriptor binds just because the pipeline object is already current can leave stale descriptor sets in place for repeated transient passes.

## Skybox Runtime Reload

Runtime `skybox.load(...)` exercises this path: `SkyBox::LoadSkyBox()` loads an equirect HDR, dispatches `EquirectangularToCubemap.hlsl` into cubemap mip 0, then dispatches `PrefilterCubemap.hlsl` through another transient compute `PassInfo` to fill roughness-filtered mips for IBL. Startup and runtime reloads should both produce nonzero cubemap memory; a black cubemap after runtime reload points first at descriptor/pipeline binding and transient-pass lifetime.

IBL specular sampling expects those roughness-filtered cubemap mips, with roughness mapped linearly to the mip range (`ComputeIBL_Common()` uses `mip = roughness * (mipCount - 1)`).

A bright HDR sun is a sub-texel, very-high-radiance delta. Naive GGX importance-sampling of it during prefiltering lights up isolated texels on a regular lattice (the sample set is identical per texel, so the sun falls into the same fixed sample slots everywhere), which the floor reflection magnifies into a grid of bright squares. `PrefilterCubemap.hlsl` suppresses this with three load-time measures:

- **`MAX_PREFILTER_RADIANCE`** — clamps per-sample radiance so the delta sun has no high-contrast spike to expose.
- **`SOURCE_MIP_BIAS`** — biases each sample toward a blurrier (pre-averaged) source mip, scaled by roughness, so the integral converges to a smooth blur (low roughness stays sharp). This is the main lever; raise it if squares/blotches return, lower it if rough reflections over-blur.
- **`PrefilterSampleCount`** (in `Skybox.cpp`) — generous counts (64/128/256/512); load-time only.

Per-texel jitter was tried and removed — once the mip bias converges the integral, jitter only trades the grid for swimming grain. The prefilter is a one-time bake at skybox load/reload (`LoadSkyBox` → `PrefilterSkyboxMips`), not a per-frame pass; per-frame IBL is a single `SampleLevel` of the baked mips. If the prefilter failed to compile/run, the mips are black.

## Lines And Script Passes

- `RenderType::Lines` is reserved for hardware line-strip meshes from `Primitives::CreatePolyline(...)` / Lua `scene.attach_lines(node, {vec3...}, closed)`. These meshes are skipped by the indirect raster buckets, shadow all-mesh draw, mesh-constants culling path, and ray-tracing BLAS/TLAS triangle path. `LinesPass` draws them directly from the position stream after `LightTransparent` and before TAA, using node uniform data for transforms and material emissive (or base color) for color.
- Lua can register frame-graph callbacks with `render_graph.add_pass(name, order, fn)` and remove them with `render_graph.remove_pass(name)`. Editor and player renderers watch the script-pass registry revision and rebuild the graph before command recording when scripts add/remove passes. The callback receives the frame `CommandBuffer`; `render_graph.get_target(name)` resolves current render targets such as `viewport`, `display`, and `depthStencil` for passes that need to record their own barriers/blits/attachments. Lua `cmd:begin_pass(...)` copies its attachment table into thread-local storage that remains valid until `cmd:end_pass()`, so scripts can safely record manual render passes without owning C++ attachment vectors.

## Editor Selection Outline

`SelectionOutlinePass` is an editor raster overlay after particles. It renders the GPU `IndirectSelected` bucket into an R8 mask with depth testing, then composites a full-screen outline into the display target. The selected bucket is driven by `Mesh_Constants::editorFlags`, which is refreshed each scene update through `SceneRuntimeHooks::IsSceneNodeSelected`; editor multi-selection therefore reaches the pass without adding editor-only state to runtime scene data. The pass no-ops when no selected renderable mesh exists, so an enabled outline cannot become the only display-affecting pass for an empty selection.

The pass is gated by `GlobalSettings::selection_outline` on raster paths and is disabled by `RuntimeSceneRenderer` so player hosts do not inherit editor overlays from serialized scene settings. The user-facing controls live in the Global widget and serialize through `.pescene`: outline color, solid outer thickness, inward fade, and outward fade. HLSL/C++ push constants stay packed as three float4 lanes (`PushConstants_SelectionOutline`) for Vulkan/DX12 alignment.

Editor Lua can drive the same overlay without touching C++ state directly. `selection.set_outline_pass(...)` / `selection.set_pass(...)` update enabled/color/thickness/inner-fade/outer-fade fields, while `selection.select(...)`, `selection.find(...)`, `selection.select_by_name(...)`, `selection.select_by_path(...)`, and `selection.select_matching(...)` resolve nodes indirectly from names, hierarchy paths, `SceneNode` handles, node indices, or tables. This is the preferred AI/user scripting surface for selecting highlight targets.

## Ray Tracing Cameras And Geometry

- Full ray tracing must build camera rays differently for perspective and orthographic projections. Perspective rays originate at the camera and use the unprojected per-pixel direction. Orthographic rays originate from the unprojected per-pixel view position and share the engine's positive view-Z forward direction; Hybrid depth clamps are measured from that per-pixel origin. Using perspective-style ray origins for orthographic cameras makes 2D quads/sprites appear as giant screen-filling surfaces in full RT.
- When geometry changes, the RT descriptor set must refresh every resource whose backing object can be recreated: TLAS, scene uniforms, mesh constants, combined geometry buffer, MeshInfo buffer, material table, and image views. Primitive-cache hits that add a new scene mesh index still require a BLAS rebuild, because `m_blasByMesh` is keyed by scene mesh index. The merged BLAS buffer size must be computed with the same align-before-place math as the build loop so later BLAS ranges stay inside the allocation.
