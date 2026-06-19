#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Semaphore.h"

namespace pe
{
    static const std::unordered_map<std::string_view, PeBarrierSync> s_semStageMap = {
        {"none", PE_STAGE_NONE},
        {"vertex", PE_STAGE_VERTEX_SHADER},
        {"fragment", PE_STAGE_FRAGMENT_SHADER},
        {"early_fragment", PE_STAGE_EARLY_FRAGMENT_TESTS},
        {"late_fragment", PE_STAGE_LATE_FRAGMENT_TESTS},
        {"color_output", PE_STAGE_COLOR_ATTACHMENT_OUTPUT},
        {"compute", PE_STAGE_COMPUTE_SHADER},
        {"transfer", PE_STAGE_TRANSFER},
        {"bottom_of_pipe", PE_STAGE_BOTTOM_OF_PIPE},
        {"all_graphics", PE_STAGE_ALL_GRAPHICS},
        {"all_commands", PE_STAGE_ALL_COMMANDS},
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
                    s.SetStageFlags(LookupFlags<PeBarrierSync>(flags, s_semStageMap));
                };

                // AddStageFlags
                semType["add_stage_flags"] = [](Semaphore &s, const std::string &flags) {
                    s.AddStageFlags(LookupFlags<PeBarrierSync>(flags, s_semStageMap));
                };

                // GetStageFlags
                semType["get_stage_flags"] = [](Semaphore &s) -> uint64_t {
                    return static_cast<uint64_t>(s.GetStageFlags());
                }; });
        }
    } s_semaphoreBindings;
} // namespace pe
