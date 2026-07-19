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
                }); });
        }
    } s_skyboxBindings;
} // namespace pe
