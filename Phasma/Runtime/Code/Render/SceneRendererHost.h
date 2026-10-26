#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class CommandBuffer;
    class Image;
    class Scene;
    class SkyBox;

    // MaterialX shaders the scene still waits for (SceneRendererCore::GetOpenPBRBuildProgress).
    struct OpenPBRBuildProgress
    {
        uint32_t ready = 0;        // modules every pass that draws them already holds
        uint32_t total = 0;        // modules the scene's passes want, failed and rejected ones aside
        bool hitsBuilding = false; // the ray-traced hit groups for the scene's modules are compiling

        bool Busy() const { return ready < total || hitsBuilding; }
    };

    class SceneRendererHost
    {
    public:
        virtual ~SceneRendererHost() = default;

        virtual Scene &GetScene() = 0;
        virtual const SkyBox &GetSkyBox() const = 0;
        virtual Image *GetIBL_LUT() const = 0;
        virtual void ReloadSkyFromSettings() = 0;

        virtual Image *CreateRenderTarget(const std::string &name,
                                          ::PeFormat format,
                                          PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                          bool useRenderTergetScale = true,
                                          bool useMips = false,
                                          vec4 clearColor = Color::Transparent) = 0;
        virtual Image *CreateDepthStencilTarget(const std::string &name,
                                                ::PeFormat format,
                                                PeImageUsageFlags usage = PE_IMAGE_USAGE_NONE,
                                                bool useRenderTergetScale = true,
                                                float clearDepth = Color::Depth,
                                                uint32_t clearStencil = Color::Stencil) = 0;
        virtual Image *GetRenderTarget(const std::string &name) = 0;
        virtual Image *GetRenderTarget(size_t hash) = 0;
        virtual bool DestroyRenderTarget(const std::string &name) = 0;
        virtual Image *GetDepthStencilTarget(const std::string &name) = 0;
        virtual Image *GetDepthStencilTarget(size_t hash) = 0;
        virtual Image *GetDisplayRT() = 0;
        virtual Image *GetViewportRT() = 0;
        virtual Image *GetDepthStencilRT() = 0;
        virtual Image *CreateFSSampledImage(Image *source) = 0;
        // Scene render graph executions so far. Unlike RHI frames it skips frames that never rendered (the swapchain
        // was not ready), so passes can tell a gap in their own history from a busy GPU.
        virtual uint64_t GetRenderedFrames() const = 0;
        virtual OpenPBRBuildProgress GetOpenPBRBuildProgress() const = 0;
    };

    void SetActiveSceneRendererHost(SceneRendererHost *renderer);
    SceneRendererHost *GetActiveSceneRendererHost();
    SceneRendererHost &RequireActiveSceneRendererHost();
} // namespace pe
