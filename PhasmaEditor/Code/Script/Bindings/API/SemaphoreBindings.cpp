#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Semaphore.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::PipelineStageFlags2> s_semStageMap = {
        {"none", vk::PipelineStageFlagBits2::eNone},
        {"vertex", vk::PipelineStageFlagBits2::eVertexShader},
        {"fragment", vk::PipelineStageFlagBits2::eFragmentShader},
        {"early_fragment", vk::PipelineStageFlagBits2::eEarlyFragmentTests},
        {"late_fragment", vk::PipelineStageFlagBits2::eLateFragmentTests},
        {"color_output", vk::PipelineStageFlagBits2::eColorAttachmentOutput},
        {"compute", vk::PipelineStageFlagBits2::eComputeShader},
        {"transfer", vk::PipelineStageFlagBits2::eTransfer},
        {"bottom_of_pipe", vk::PipelineStageFlagBits2::eBottomOfPipe},
        {"all_graphics", vk::PipelineStageFlagBits2::eAllGraphics},
        {"all_commands", vk::PipelineStageFlagBits2::eAllCommands},
    };

    static struct SemaphoreBindings
    {
        SemaphoreBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<Semaphore> semType = lua.new_usertype<Semaphore>("Semaphore", sol::no_constructor);

                // Factory
                lua.set_function("create_semaphore", [](bool timeline, const std::string &name) -> Semaphore * {
                    return Semaphore::Create(timeline, name);
                });
                lua.set_function("destroy_semaphore", [](Semaphore *s) {
                    if (s) Semaphore::Destroy(s);
                });

                // IsTimeline
                semType["is_timeline"] = sol::property(&Semaphore::IsTimeline);

                // Wait
                semType["wait"] = &Semaphore::Wait;

                // Signal
                semType["signal"] = &Semaphore::Signal;

                // GetValue
                semType["get_value"] = sol::property(&Semaphore::GetValue);

                // SetStageFlags
                semType["set_stage_flags"] = [](Semaphore &s, const std::string &flags) {
                    s.SetStageFlags(LookupFlags<vk::PipelineStageFlags2>(flags, s_semStageMap));
                };

                // AddStageFlags
                semType["add_stage_flags"] = [](Semaphore &s, const std::string &flags) {
                    s.AddStageFlags(LookupFlags<vk::PipelineStageFlags2>(flags, s_semStageMap));
                };

                // GetStageFlags
                semType["get_stage_flags"] = [](Semaphore &s) -> uint64_t {
                    return static_cast<uint64_t>(static_cast<vk::PipelineStageFlags2::MaskType>(s.GetStageFlags()));
                }; });
        }
    } s_semaphoreBindings;
} // namespace pe
