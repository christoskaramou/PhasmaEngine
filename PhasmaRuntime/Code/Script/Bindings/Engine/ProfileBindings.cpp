#include "Script/ScriptSystem.h"

#ifdef PE_TRACY
#include <tracy/TracyC.h>
#endif

namespace pe
{
#ifdef PE_TRACY
    static thread_local std::vector<TracyCZoneCtx> s_luaTracyZones;
#endif

    static struct ProfileBindings
    {
        ProfileBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                lua.set_function("profile_begin", [](const std::string &name) {
                    Profiler::BeginScope(name.c_str());
#ifdef PE_TRACY
                    TracyCZoneN(ctx, name.c_str(), true);
                    s_luaTracyZones.push_back(ctx);
#endif
                });

                lua.set_function("profile_end", []() {
                    Profiler::EndScope();
#ifdef PE_TRACY
                    if (!s_luaTracyZones.empty())
                    {
                        TracyCZoneEnd(s_luaTracyZones.back());
                        s_luaTracyZones.pop_back();
                    }
#endif
                }); });
        }
    } s_profileBindings;
} // namespace pe
