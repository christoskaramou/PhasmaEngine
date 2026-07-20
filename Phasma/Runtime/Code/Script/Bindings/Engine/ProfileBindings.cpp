#include "Script/ScriptSystem.h"

#ifdef PE_TRACY
#include <tracy/TracyC.h>
#endif

namespace pe
{
#ifdef PE_TRACY
    static thread_local std::vector<TracyCZoneCtx> s_luaTracyZones;
#endif

    // BeginScope stores a const char* until the frame is published. Intern names
    // for process lifetime — never free on profile_end (that caused garbled
    // ATH/* scope names in the Android stream).
    static std::mutex s_luaNameMutex;
    static std::unordered_set<std::string> s_luaInternedNames;

    static const char *InternLuaProfileName(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(s_luaNameMutex);
        auto [it, inserted] = s_luaInternedNames.insert(name);
        (void)inserted;
        return it->c_str();
    }

    static struct ProfileBindings
    {
        ProfileBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                lua.set_function("profile_begin", [](const std::string &name) {
                    const char *stable = InternLuaProfileName(name);
                    Profiler::BeginScope(stable);
#ifdef PE_TRACY
                    TracyCZoneN(ctx, stable, true);
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
