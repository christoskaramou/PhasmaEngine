#pragma once

#include "API/RHITypes.h"
#include "Base/Math.h"

namespace pe
{
    class CommandBuffer;
    class Image;
    class Scene;
    class SkyBox;

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
        virtual Image *CreateFSSampledImage(bool useRenderTergetScale = true) = 0;
    };

    void SetActiveSceneRendererHost(SceneRendererHost *renderer);
    SceneRendererHost *GetActiveSceneRendererHost();
    SceneRendererHost &RequireActiveSceneRendererHost();
} // namespace pe
