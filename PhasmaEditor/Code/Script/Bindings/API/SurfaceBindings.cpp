#include "Script/ScriptSystem.h"
#include "Script/Bindings/BindingUtils.h"
#include "API/Surface.h"

namespace pe
{
    static const std::unordered_map<std::string_view, vk::PresentModeKHR> s_surfPresentModeMap = {
        {"immediate", vk::PresentModeKHR::eImmediate},
        {"mailbox", vk::PresentModeKHR::eMailbox},
        {"fifo", vk::PresentModeKHR::eFifo},
        {"fifo_relaxed", vk::PresentModeKHR::eFifoRelaxed},
    };

    static std::string PresentModeStr(vk::PresentModeKHR mode)
    {
        for (auto &[k, v] : s_surfPresentModeMap)
            if (v == mode)
                return std::string(k);
        return "unknown";
    }

    static struct SurfaceBindings
    {
        SurfaceBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::usertype<Surface> surfType = lua.new_usertype<Surface>("Surface", sol::no_constructor);

                // SetPresentMode
                surfType["set_present_mode"] = [](Surface &s, const std::string &mode) {
                    auto it = s_surfPresentModeMap.find(std::string_view(mode));
                    if (it != s_surfPresentModeMap.end())
                        s.SetPresentMode(it->second);
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
                    return PresentModeStr(s.GetPresentMode());
                };

                // GetSupportedPresentModes
                surfType["get_supported_present_modes"] = [&lua](Surface &s) -> sol::table {
                    auto modes = s.GetSupportedPresentModes();
                    sol::table t = lua.create_table();
                    for (size_t i = 0; i < modes.size(); i++)
                        t[i + 1] = PresentModeStr(modes[i]);
                    return t;
                }; });
        }
    } s_surfaceBindings;
} // namespace pe
