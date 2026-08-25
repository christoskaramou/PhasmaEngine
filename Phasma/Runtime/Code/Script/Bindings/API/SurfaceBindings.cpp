#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Surface.h"
#include "Runtime/RuntimeStartup.h"

namespace pe
{
    static struct SurfaceBindings
    {
        SurfaceBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<Surface> surfType = lua.new_usertype<Surface>("Surface", sol::no_constructor);

                // SetPresentMode
                surfType["set_present_mode"] = [](Surface &s, const std::string &mode) {
                    if (const std::optional<PePresentMode> parsed = ParsePresentModeToken(mode))
                        s.SetPresentMode(*parsed);
                };

                // GetActualExtent (width/height)
                surfType["get_width"] = sol::property([](Surface &s) -> uint32_t {
                    return s.GetActualExtent().width;
                });
                surfType["get_height"] = sol::property([](Surface &s) -> uint32_t {
                    return s.GetActualExtent().height;
                });

                // GetFormat
                surfType["get_format"] = sol::property([](Surface &s) -> int {
                    return static_cast<int>(s.GetFormat());
                });

                // GetColorSpace
                surfType["get_color_space"] = sol::property([](Surface &s) -> int {
                    return static_cast<int>(s.GetColorSpace());
                });

                // GetPresentMode
                surfType["get_present_mode"] = [](Surface &s) -> std::string {
                    return PresentModeToConfigToken(s.GetPresentMode());
                };

                // GetSupportedPresentModes
                surfType["get_supported_present_modes"] = [](Surface &s, sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    auto modes = s.GetSupportedPresentModes();
                    sol::table t = lua.create_table();
                    for (size_t i = 0; i < modes.size(); i++)
                        t[i + 1] = PresentModeToConfigToken(modes[i]);
                    return t;
                }; });
        }
    } s_surfaceBindings;
} // namespace pe
