#include "Render/SceneRendererHost.h"

namespace pe
{
    namespace
    {
        // TODO(shared-runtime): move this state behind the shared runtime image if
        // PhasmaRuntime stops being linked as a static library.
        SceneRendererHost *s_activeSceneRendererHost = nullptr;
    } // namespace

    void SetActiveSceneRendererHost(SceneRendererHost *renderer)
    {
        s_activeSceneRendererHost = renderer;
    }

    SceneRendererHost *GetActiveSceneRendererHost()
    {
        return s_activeSceneRendererHost;
    }

    SceneRendererHost &RequireActiveSceneRendererHost()
    {
        if (!s_activeSceneRendererHost)
            PE_ERROR("[Renderer] Active scene renderer host is not registered");
        return *s_activeSceneRendererHost;
    }
} // namespace pe
