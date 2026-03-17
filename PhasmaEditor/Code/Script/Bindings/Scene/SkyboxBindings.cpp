#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Skybox/Skybox.h"
#include "Systems/RendererSystem.h"
#include "RenderPasses/LightPass.h"
#include "RenderPasses/RayTracingPass.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "API/Command.h"

namespace pe
{
    static void UpdateSkyboxDescriptors()
    {
        if (auto *pass = GetGlobalComponent<LightOpaquePass>())
            pass->UpdateDescriptorSets();
        if (auto *pass = GetGlobalComponent<LightTransparentPass>())
            pass->UpdateDescriptorSets();
        if (auto *pass = GetGlobalComponent<RayTracingPass>())
            pass->UpdateDescriptorSets();
    }
    static struct SkyboxBindings
    {
        SkyboxBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table skybox = lua.create_named_table("skybox");

                skybox.set_function("load", [](const std::string &path, sol::optional<std::string> time) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;

                    std::string fullPath = path;
                    if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos)
                        fullPath = Path::Assets + "Skybox/" + path;

                    if (!std::filesystem::exists(fullPath))
                    {
                        PE_WARN("skybox.load: file not found: %s", fullPath.c_str());
                        return;
                    }

                    std::string t = time.value_or("day");
                    Queue *queue = RHII.GetMainQueue();
                    CommandBuffer *cmd = queue->AcquireCommandBuffer();
                    cmd->Begin();
                    SkyBox &sb = const_cast<SkyBox &>(t == "night" ? r->GetSkyBoxNight() : r->GetSkyBoxDay());
                    sb.Destroy();
                    sb.LoadSkyBox(cmd, fullPath);
                    cmd->End();
                    queue->Submit(1, &cmd, nullptr, nullptr);
                    cmd->Wait();
                    cmd->Return();

                    Settings::Get<GlobalSettings>().day = (t == "day");
                    UpdateSkyboxDescriptors();
                });

                skybox.set_function("set_time", [](const std::string &time) {
                    Settings::Get<GlobalSettings>().day = (time == "day");
                    UpdateSkyboxDescriptors();
                });

                skybox.set_function("is_day", []() -> bool {
                    return Settings::Get<GlobalSettings>().day;
                }); });
        }
    } s_skyboxBindings;
} // namespace pe
#endif
