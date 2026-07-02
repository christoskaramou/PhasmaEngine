#pragma once

#include "API/RenderGraph.h"

#include <cstddef>
#include <span>

namespace pe
{
    class CommandBuffer;
    class IRenderPassComponent;
    class Scene;

    enum class SceneRenderGraphPassId : uint32_t
    {
        Culling = 0,
        Shadow,
        CullPhase1,
        Depth,
        DepthPyramid,
        OcclusionCulling, // two-phase Hi-Z: phase-2 cull (CullPhase2)
        DepthLate,
        GBufferOpaque,
        VoxelHiZ, // temporal voxel occlusion: build voxel-inclusive Hi-Z from post-G-buffer depth
        SSAO,
        ForwardPlusLightCulling,
        LightOpaque,
        GBufferTransparent,
        LightTransparent,
        Lines,
        RayTracing,
        ParticleCompute,
        Particle,
        SSR,
        FXAA,
        Aabbs,
        TAA,
        Sharpen,
        Upsample,
        Tonemap,
        ColorGrading,
        BloomBF,
        BloomH,
        BloomV,
        DOF,
        MotionBlur,
        Grid,
        SelectionOutline,
        Count
    };

    inline constexpr size_t kSceneRenderGraphPassCount = static_cast<size_t>(SceneRenderGraphPassId::Count);

    struct SceneRenderGraphPassComponents
    {
        IRenderPassComponent *culling = nullptr;
        IRenderPassComponent *shadow = nullptr;
        IRenderPassComponent *cullPhase1 = nullptr;
        IRenderPassComponent *depth = nullptr;
        IRenderPassComponent *depthPyramid = nullptr;
        IRenderPassComponent *occlusionCulling = nullptr; // two-phase Hi-Z: phase-2 cull
        IRenderPassComponent *depthLate = nullptr;
        IRenderPassComponent *gbufferOpaque = nullptr;
        IRenderPassComponent *voxelHiZPyramid = nullptr; // temporal voxel occlusion pyramid
        IRenderPassComponent *ssao = nullptr;
        IRenderPassComponent *forwardPlusLightCulling = nullptr;
        IRenderPassComponent *lightOpaque = nullptr;
        IRenderPassComponent *gbufferTransparent = nullptr;
        IRenderPassComponent *lightTransparent = nullptr;
        IRenderPassComponent *lines = nullptr;
        IRenderPassComponent *rayTracing = nullptr;
        IRenderPassComponent *particleCompute = nullptr;
        IRenderPassComponent *particle = nullptr;
        IRenderPassComponent *ssr = nullptr;
        IRenderPassComponent *fxaa = nullptr;
        IRenderPassComponent *aabbs = nullptr;
        IRenderPassComponent *taa = nullptr;
        IRenderPassComponent *sharpen = nullptr;
        IRenderPassComponent *upsample = nullptr;
        IRenderPassComponent *tonemap = nullptr;
        IRenderPassComponent *colorGrading = nullptr;
        IRenderPassComponent *bloomBrightFilter = nullptr;
        IRenderPassComponent *bloomGaussianBlurHorizontal = nullptr;
        IRenderPassComponent *bloomGaussianBlurVertical = nullptr;
        IRenderPassComponent *dof = nullptr;
        IRenderPassComponent *motionBlur = nullptr;
        IRenderPassComponent *grid = nullptr;
        IRenderPassComponent *selectionOutline = nullptr;
    };

    using SceneRenderGraphPassCondition = std::function<bool(SceneRenderGraphPassId)>;

    void AddSceneRenderGraphPasses(RenderGraph &renderGraph,
                                   const SceneRenderGraphPassComponents &components,
                                   SceneRenderGraphPassCondition isPassEnabled);

    void CreateSceneRenderGraphPassComponents(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                              bool includeRayTracingPass);

    void InitEnabledSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                   SceneRenderGraphPassCondition isPassEnabled,
                                                   std::span<bool> passInitialized,
                                                   CommandBuffer *cmd);

    // Resizes every initialized component, enabled or not: once a pass component is initialized it is
    // kept alive until scene teardown (disabled passes just stop being recorded). Destroying components
    // on render-mode toggles and re-initializing them later caused intermittent VK_ERROR_DEVICE_LOST
    // and per-swapchain-image descriptor staleness (alternating-frame flicker).
    void ResizeInitializedSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                         std::span<bool> passInitialized,
                                                         uint32_t width,
                                                         uint32_t height);

    void DestroyInitializedSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                          std::span<bool> passInitialized);

    [[nodiscard]] SceneRenderGraphPassComponents GetGlobalSceneRenderGraphPassComponents();

    void UpdateSceneRenderGraphPassStates(std::span<bool> passEnabled, bool hasRayTracingGeometry);

    void UpdateSceneRenderGraphPassComponents(const OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                              const SceneRenderGraphPassComponents &components,
                                              const SceneRenderGraphPassCondition &isPassEnabled);

    void SetSceneRenderGraphPassScene(const SceneRenderGraphPassComponents &components, Scene &scene);
} // namespace pe
