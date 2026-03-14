#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "API/RenderPass.h"

namespace pe
{
    static struct RenderPassBindings
    {
        RenderPassBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                // RenderPass type (no public methods beyond ctor/dtor)
                sol::usertype<RenderPass> rpType = lua.new_usertype<RenderPass>("RenderPass", sol::no_constructor);

                lua.set_function("reload_scripts", []() {
                    GetGlobalSystem<ScriptSystem>()->Reload();
                }); });
        }
    } s_renderPassBindings;
} // namespace pe
#endif
