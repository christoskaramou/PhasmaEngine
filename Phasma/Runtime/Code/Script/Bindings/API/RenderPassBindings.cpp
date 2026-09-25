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

                // Deferred: reloading synchronously would destroy the Lua state this call is running on.
                lua.set_function("reload_scripts", []() {
                    EventSystem::PushEvent(EventType::CompileScripts);
                }); });
        }
    } s_renderPassBindings;
} // namespace pe
