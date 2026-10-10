#pragma once

#include "API/RHITypes.h"

#include <cstddef>

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
                                   vec4 clearColor = Color::Transparent);

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

    void BlitSceneImageToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex);
} // namespace pe
