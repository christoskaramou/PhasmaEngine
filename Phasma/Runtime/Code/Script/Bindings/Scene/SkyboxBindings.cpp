#include "Script/ScriptSystem.h"
#include "Render/SceneSky.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneRuntimeHooks.h"

namespace pe
{
    static struct SkyboxBindings
    {
        SkyboxBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table skybox = lua.create_named_table("skybox");

                skybox.set_function("load", [](const std::string &path) {
                    std::string fullPath = path;
                    if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos)
                        fullPath = Path::ResolveAsset("Skybox/" + path);

                    if (!AssetFileExists(fullPath))
                    {
                        PE_WARN("[Lua] skybox.load: file not found: %s", fullPath.c_str());
                        return;
                    }

                    const std::string normalizedPath = MakeSceneSkyPathSetting(fullPath);
                    auto &settings = Settings::Get<SceneSettings>();

                    if (Scene *scene = GetActiveScene())
                    {
                        NodeId *skyboxNode = scene->GetSkyboxNode();
                        if (!skyboxNode)
                            skyboxNode = scene->CreateSkyboxNode();

                        if (skyboxNode)
                        {
                            scene->SetSkyboxPath(skyboxNode, normalizedPath);
                            return;
                        }
                    }

                    settings.skybox_path = normalizedPath;
                    RefreshSceneSky();
                });

                // Degrees, applied X, then Y, then Z; a change reloads the sky.
                skybox.set_function("set_rotation", [](float x, float y, float z) {
                    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                    {
                        PE_WARN("[Lua] skybox.set_rotation: angles must be finite");
                        return;
                    }

                    auto &settings = Settings::Get<SceneSettings>();
                    const std::array<float, 3> rotation{x, y, z};
                    if (settings.skybox_rotation == rotation)
                        return;

                    settings.skybox_rotation = rotation;
                    if (Scene *scene = GetActiveScene())
                        scene->MarkDirty();

                    RefreshSceneSky();
                });

                skybox.set_function("get_rotation", []() {
                    const auto &rotation = Settings::Get<SceneSettings>().skybox_rotation;
                    return std::make_tuple(rotation[0], rotation[1], rotation[2]);
                }); });
        }
    } s_skyboxBindings;
} // namespace pe
