#include "Script/ScriptSystem.h"
#include "API/Framebuffer.h"

namespace pe
{
    static struct FramebufferBindings
    {
        FramebufferBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<Framebuffer> fbType = lua.new_usertype<Framebuffer>("Framebuffer", sol::no_constructor);

                fbType["get_size"] = [](Framebuffer &fb) -> std::tuple<uint32_t, uint32_t> {
                    uvec2 s = fb.GetSize();
                    return {s.x, s.y};
                };
                fbType["get_width"] = sol::property(&Framebuffer::GetWidth);
                fbType["get_height"] = sol::property(&Framebuffer::GetHeight); });
        }
    } s_framebufferBindings;
} // namespace pe
