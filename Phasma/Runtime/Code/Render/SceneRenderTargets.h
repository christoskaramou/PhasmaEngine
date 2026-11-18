#pragma once

#include "API/RHITypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pe
{
    class CommandBuffer;
    class Image;

    using SceneRenderTargetMap = std::unordered_map<size_t, Image *>;

    struct SceneRenderTargets
    {
        Image *depthStencil = nullptr;
        Image *viewport = nullptr;
        Image *display = nullptr;
        Image *screenshot = nullptr;
    };

    // Velocity follows active TAA/motion blur and backend raster requirements.
    bool SceneNeedsVelocityRT(bool hasRayTracingGeometry);
    bool SceneUsesHDR();
    bool SceneUsesLinearColor();
    // openpbr is on and this platform/GPU runs the MaterialX visibility-buffer path (warns once when it cannot).
    bool SceneUsesOpenPBR();
    bool SceneUsesOpenPBRDebug();
    // The device has no bindless textures (WebGPU, or Vulkan under PHASMA_CAPS_PROFILE=webgpu): the web profile's
    // shader variants and per-mesh draws (WebGPU RHI spec decision 6).
    bool SceneUsesWebProfile();
    // Under the web profile, turns off the settings the web cannot run, caps num_cascades and snaps the shadow settings
    // to the lists below; called where they change.
    void ApplyWebProfileSettings();

    // The shadow map sizes and cascade counts the web profile allows. Each pair is a LightingPS variant, and the web
    // editor bake (tools/bake_web_shaders.py --editor) compiles all of them because the browser cannot.
    inline constexpr std::array<uint32_t, 3> kWebShadowMapSizes{1024, 2048, 4096};
    inline constexpr uint32_t kWebMaxShadowCascades = 4;
    // An sRGB-authored colour as the frame uses it: decoded in a linear-colour scene (values above 1 keep their
    // brightness), unchanged otherwise.
    vec3 SceneColor(const vec3 &srgb);
    uint32_t SceneColorPacked(uint32_t rgba8);

    Image *CreateSceneRenderTarget(SceneRenderTargetMap &renderTargets,
                                   const std::string &name,
                                   ::PeFormat format,
                                   PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                   bool useRenderTargetScale = true,
                                   bool useMips = false,
                                   vec4 clearColor = Color::Transparent,
                                   bool storage = true);

    Image *CreateSceneDepthStencilTarget(SceneRenderTargetMap &depthStencilTargets,
                                         const std::string &name,
                                         ::PeFormat format,
                                         PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                         bool useRenderTargetScale = true,
                                         float clearDepth = Color::Depth,
                                         uint32_t clearStencil = Color::Stencil);

    Image *GetSceneRenderTarget(const SceneRenderTargetMap &renderTargets, const std::string &name);
    Image *GetSceneRenderTarget(const SceneRenderTargetMap &renderTargets, size_t hash);
    bool DestroySceneRenderTarget(SceneRenderTargetMap &renderTargets, const std::string &name);

    Image *CreateSceneFSSampledImage(const std::string &name, Image *source);

    SceneRenderTargets CreateDefaultSceneRenderTargets(SceneRenderTargetMap &renderTargets,
                                                       SceneRenderTargetMap &depthStencilTargets,
                                                       bool hasRayTracingGeometry,
                                                       bool scaleOutput);

    // Synchronize optional velocity membership and configurable normal/velocity formats. Returns
    // true if membership or format changed (callers should refresh GBuffer pipelines/attachments).
    bool SyncOptionalSceneRenderTargets(SceneRenderTargetMap &renderTargets, bool hasRayTracingGeometry);

    void DestroySceneRenderTargets(SceneRenderTargetMap &renderTargets, SceneRenderTargetMap &depthStencilTargets);
    // Bumped whenever a scene target is destroyed: caches keyed by target pointers are stale once it changes.
    uint64_t GetSceneRenderTargetGeneration();

    void BlitSceneImageToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex);
} // namespace pe
