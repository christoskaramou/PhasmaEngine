#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"

namespace pe
{
    static struct EngineBindings
    {
        EngineBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table engine = lua.create_named_table("engine");

                engine.set_function("get_metrics", [&lua]() -> sol::table {
                    sol::table t = lua.create_table();
                    double dt = FrameTimer::Instance().GetDelta();
                    t["fps"] = dt > 0.0 ? 1.0 / dt : 0.0;
                    t["delta_ms"] = dt * 1000.0;
                    return t;
                });

                engine.set_function("compile_shaders", []() {
                    EventSystem::PushEvent(EventType::CompileShaders);
                }); });
        }
    } s_engineBindings;
} // namespace pe
#endif
