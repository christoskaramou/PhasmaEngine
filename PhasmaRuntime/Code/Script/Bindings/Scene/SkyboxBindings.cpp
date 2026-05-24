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

                skybox.set_function("load", [](const std::string &path, sol::optional<std::string> time) {
                    std::string fullPath = path;
                    if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos)
                        fullPath = Path::Assets + "Skybox/" + path;

                    if (!std::filesystem::exists(fullPath))
                    {
                        PE_WARN("[Lua] skybox.load: file not found: %s", fullPath.c_str());
                        return;
                    }

                    std::string t = time.value_or("day");
                    const bool useDaySky = t != "night";
                    const std::string normalizedPath = MakeSceneSkyPathSetting(fullPath);
                    auto &settings = Settings::Get<GlobalSettings>();
                    settings.day = useDaySky;

                    if (Scene *scene = GetActiveScene())
                    {
                        NodeId *skyboxNode = scene->GetSkyboxNode();
                        if (!skyboxNode)
                            skyboxNode = scene->CreateSkyboxNode();

                        if (skyboxNode)
                        {
                            std::string dayPath = settings.skybox_day_path;
                            std::string nightPath = settings.skybox_night_path;
                            if (auto *skybox = scene->GetSkyboxForNode(skyboxNode))
                            {
                                dayPath = skybox->dayPath;
                                nightPath = skybox->nightPath;
                            }
                            if (useDaySky)
                                dayPath = normalizedPath;
                            else
                                nightPath = normalizedPath;
                            scene->SetSkyboxPaths(skyboxNode, std::move(dayPath), std::move(nightPath));
                            return;
                        }
                    }

                    std::string &skyboxPath = useDaySky ? settings.skybox_day_path : settings.skybox_night_path;
                    skyboxPath = normalizedPath;
                    RefreshSceneSky();
                });

                skybox.set_function("set_time", [](const std::string &time) {
                    Settings::Get<GlobalSettings>().day = (time == "day");
                    RefreshSceneRenderDescriptors();
                });

                skybox.set_function("is_day", []() -> bool {
                    return Settings::Get<GlobalSettings>().day;
                }); });
        }
    } s_skyboxBindings;
} // namespace pe
