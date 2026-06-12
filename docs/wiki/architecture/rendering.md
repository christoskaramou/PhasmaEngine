# Rendering And RHI Notes

The render path is split between PhasmaRuntime's shared scene renderer and PhasmaCore's backend-neutral RHI wrappers. Code remains the source of truth; use this page for cross-cutting pitfalls that are easy to miss when reading one file at a time.

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
