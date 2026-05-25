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
        Depth,
        GBufferOpaque,
        SSAO,
        LightOpaque,
        GBufferTransparent,
        LightTransparent,
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
        BloomBF,
        BloomH,
        BloomV,
        DOF,
        MotionBlur,
        Grid,
        Count
    };

    inline constexpr size_t kSceneRenderGraphPassCount = static_cast<size_t>(SceneRenderGraphPassId::Count);

    struct SceneRenderGraphPassComponents
    {
        IRenderPassComponent *culling = nullptr;
        IRenderPassComponent *shadow = nullptr;
        IRenderPassComponent *depth = nullptr;
        IRenderPassComponent *gbufferOpaque = nullptr;
        IRenderPassComponent *ssao = nullptr;
        IRenderPassComponent *lightOpaque = nullptr;
        IRenderPassComponent *gbufferTransparent = nullptr;
        IRenderPassComponent *lightTransparent = nullptr;
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
        IRenderPassComponent *bloomBrightFilter = nullptr;
        IRenderPassComponent *bloomGaussianBlurHorizontal = nullptr;
        IRenderPassComponent *bloomGaussianBlurVertical = nullptr;
        IRenderPassComponent *dof = nullptr;
        IRenderPassComponent *motionBlur = nullptr;
        IRenderPassComponent *grid = nullptr;
    };

    using SceneRenderGraphPassCondition = std::function<bool(SceneRenderGraphPassId)>;

    void AddSceneRenderGraphPasses(RenderGraph &renderGraph,
                                   const SceneRenderGraphPassComponents &components,
                                   SceneRenderGraphPassCondition isPassEnabled);

    void CreateSceneRenderGraphPassComponents(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                              bool includeRayTracingPass);

    void InitSceneRenderGraphPassComponents(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                            CommandBuffer *cmd);

    void ResizeSceneRenderGraphPassComponents(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                              uint32_t width,
                                              uint32_t height);

    void DestroySceneRenderGraphPassComponents(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents);

    [[nodiscard]] SceneRenderGraphPassComponents GetGlobalSceneRenderGraphPassComponents();

    void UpdateSceneRenderGraphPassStates(std::span<bool> passEnabled, bool hasRayTracingGeometry);

    void UpdateSceneRenderGraphPassComponents(const OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                              const SceneRenderGraphPassComponents &components,
                                              const SceneRenderGraphPassCondition &isPassEnabled);

    void SetSceneRenderGraphPassScene(const SceneRenderGraphPassComponents &components, Scene &scene);
} // namespace pe
