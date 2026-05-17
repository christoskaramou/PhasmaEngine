#pragma once

namespace pe
{
    class Scene;

    using ActiveSceneGetter = Scene *(*)();

    void SetActiveSceneGetter(ActiveSceneGetter getter);
    [[nodiscard]] Scene *GetActiveScene();
} // namespace pe
