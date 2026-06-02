# Rendering And RHI Notes

The render path is split between PhasmaRuntime's shared scene renderer and PhasmaCore's backend-neutral RHI wrappers. Code remains the source of truth; use this page for cross-cutting pitfalls that are easy to miss when reading one file at a time.

## Cached Pipelines

- `CommandBuffer::GetPipeline()` caches `Pipeline` objects globally from a `PassInfo` hash. Some callers, such as skybox HDR-to-cubemap conversion, build transient `PassInfo` instances and destroy them after the command buffer finishes.
- A cached `Pipeline` must treat the source `PassInfo` as construction-only state. Runtime command recording should use immutable metadata copied into `Pipeline` at creation time, such as pipeline type and push-constant ranges, instead of reading through the original `PassInfo` reference.
- Vulkan descriptor auto-binding must still run when rebinding resources for the same cached pipeline. Skipping descriptor binds just because the pipeline object is already current can leave stale descriptor sets in place for repeated transient passes.

## Skybox Runtime Reload

Runtime `skybox.load(...)` exercises this path: `SkyBox::LoadSkyBox()` loads an equirect HDR, generates mips through the shared Downsampler, then dispatches `EquirectangularToCubemap.hlsl` through a transient compute `PassInfo`. Startup and runtime reloads should both produce nonzero cubemap memory; a black cubemap after runtime reload points first at descriptor/pipeline binding and transient-pass lifetime.
