#pragma once

#include "API/RHITypes.h"

#include <filesystem>
#include <optional>
#include <string>

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
        virtual const SkyBox &GetSkyBoxDay() const = 0;
        virtual const SkyBox &GetSkyBoxNight() const = 0;
        virtual Image *GetIBL_LUT() const = 0;

        virtual Image *GetRenderTarget(const std::string &name) = 0;
        virtual Image *GetRenderTarget(size_t hash) = 0;
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
