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
        GlobalIllumination,
        GBufferTransparent,
        LightTransparent,
        Lines,
        RayTracing,
        RTDepthResolve, // full RT only: stamp ray-traced primary-hit depth into the depth buffer
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
        ScreenProbes,    // Lumen-style screen probe gather for the opaque lighting pass
        Reflections,     // Lumen-style ray-traced reflections for the opaque lighting pass
        VisibilityDebug, // openpbr_debug: quad-exact reconstruction check of the visibility payload
        MaterialThin,    // openpbr: material pass 1 (thin G-buffer)
        MaterialShade,   // openpbr: material pass 2 (MaterialX shading)
        OpenPBRSkinning,
        OpenPBRForward, // openpbr: MaterialX alpha-blend and transmission meshes (forward pass)
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
        IRenderPassComponent *globalIllumination = nullptr;
        IRenderPassComponent *screenProbes = nullptr;
        IRenderPassComponent *reflections = nullptr;
        IRenderPassComponent *gbufferTransparent = nullptr;
        IRenderPassComponent *lightTransparent = nullptr;
        IRenderPassComponent *lines = nullptr;
        IRenderPassComponent *rayTracing = nullptr;
        IRenderPassComponent *rtDepthResolve = nullptr; // full RT depth stamp for overlay depth tests
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
        IRenderPassComponent *visibilityDebug = nullptr;
        IRenderPassComponent *materialThin = nullptr;
        IRenderPassComponent *materialShade = nullptr;
        IRenderPassComponent *openpbrSkinning = nullptr;
        IRenderPassComponent *openpbrForward = nullptr;
    };

    using SceneRenderGraphPassCondition = std::function<bool(SceneRenderGraphPassId)>;

    // Sort-number overrides saved with the scene (SceneSettings::render_pass_orders, keyed by pass name,
    // "script:<name>" for script passes).
    uint32_t GetSceneRenderGraphPassOrder(RenderGraph::PassID passId, const std::string &name, uint32_t defaultOrder);
    // The built-in sort number (HDR and LDR place tone mapping, sharpening, FXAA and colour grading differently).
    uint32_t DefaultSceneRenderGraphPassOrder(SceneRenderGraphPassId passId);

    void AddSceneRenderGraphPasses(RenderGraph &renderGraph,
                                   const SceneRenderGraphPassComponents &components,
                                   SceneRenderGraphPassCondition isPassEnabled);

    void CreateSceneRenderGraphPassComponents(OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                              bool includeRayTracingPass);

    // Frames a pass stays initialized after it was last enabled, so a brief toggle does not rebuild it.
    inline constexpr uint32_t kSceneRenderGraphPassReleaseFrames = 30;

    // Initializes enabled components that are not yet initialized and stamps every enabled pass with the RHI frame
    // counter.
    void InitEnabledSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                   SceneRenderGraphPassCondition isPassEnabled,
                                                   std::span<bool> passInitialized,
                                                   std::span<uint32_t> passLastEnabledFrame,
                                                   CommandBuffer *cmd);

    // Destroys components whose pass has not been enabled for kSceneRenderGraphPassReleaseFrames (and more frames
    // than are in flight) and retires their cached pipelines; they initialize again when the pass is re-enabled.
    void ReleaseDisabledSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                       std::span<bool> passInitialized,
                                                       std::span<const uint32_t> passLastEnabledFrame);

    // Resizes every initialized component, enabled or not (a disabled one is initialized until it is released).
    void ResizeInitializedSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                         std::span<bool> passInitialized,
                                                         uint32_t width,
                                                         uint32_t height,
                                                         bool rebuildPipelines);

    void DestroyInitializedSceneRenderGraphPassComponents(const SceneRenderGraphPassComponents &components,
                                                          std::span<bool> passInitialized);

    [[nodiscard]] SceneRenderGraphPassComponents GetGlobalSceneRenderGraphPassComponents();

    void UpdateSceneRenderGraphPassStates(std::span<bool> passEnabled, bool hasRayTracingGeometry);

    void UpdateSceneRenderGraphPassComponents(const OrderedMap<size_t, IRenderPassComponent *> &renderPassComponents,
                                              const SceneRenderGraphPassComponents &components,
                                              const SceneRenderGraphPassCondition &isPassEnabled);

    void SetSceneRenderGraphPassScene(const SceneRenderGraphPassComponents &components, Scene &scene);
} // namespace pe
