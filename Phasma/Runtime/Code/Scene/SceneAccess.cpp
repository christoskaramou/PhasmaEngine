#include "Scene/SceneAccess.h"

namespace pe
{
    namespace
    {
        // PhasmaRuntime is static-linked into the editor host and hot-reload module,
        // so this registration is per image. Register from the image that calls it.
        // TODO(shared-runtime): move this state behind the shared runtime image if
        // PhasmaRuntime stops being linked as a static library.
        ActiveSceneGetter s_activeSceneGetter = nullptr;
    } // namespace

    void SetActiveSceneGetter(ActiveSceneGetter getter)
    {
        s_activeSceneGetter = getter;
    }

    Scene *GetActiveScene()
    {
        return s_activeSceneGetter ? s_activeSceneGetter() : nullptr;
    }
} // namespace pe
