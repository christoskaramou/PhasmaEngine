#pragma once

#include "API/RHITypes.h"
#include "Base/Settings.h"
#include "Base/Math.h"

#include <cstddef>
#include <string>
#include <unordered_map>

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

    Image *CreateSceneFSSampledImage(const std::string &name, bool useRenderTargetScale = true);

    SceneRenderTargets CreateDefaultSceneRenderTargets(SceneRenderTargetMap &renderTargets,
                                                       SceneRenderTargetMap &depthStencilTargets);

    void BlitSceneImageToSwapchain(CommandBuffer *cmd, Image *src, uint32_t imageIndex);
} // namespace pe
