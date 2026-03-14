#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    static struct SceneBindings
    {
        SceneBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                       {
                lua.set_function("save_scene", [](const std::string &name) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().SaveScene(Path::Assets + "Scenes/" + name);
                });

                lua.set_function("load_scene", [](const std::string &name) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().LoadScene(Path::Assets + "Scenes/" + name);
                }); });
        }
    } s_sceneBindings;
} // namespace pe
#endif
