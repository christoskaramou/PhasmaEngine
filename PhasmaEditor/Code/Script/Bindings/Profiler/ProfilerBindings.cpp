#include "GUI/Widgets/ProfilerWidget.h"
#include "Script/ScriptSystem.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    namespace
    {
        struct ProfilerBindings
        {
            ProfilerBindings()
            {
                ScriptSystem::AddBindings(
                    [](sol::state &lua)
                    { lua.set_function("profiler_snapshot",
                                       []() -> std::string
                                       {
                        auto *renderer = GetGlobalSystem<RendererSystem>();
                        auto *profiler = renderer ? renderer->GetGUI().GetWidget<ProfilerWidget>() : nullptr;
                        if (!profiler)
                            return std::string{};
                        return profiler->TakeSnapshot(); }); });
            }
        } s_profilerBindings;
    } // namespace
} // namespace pe
