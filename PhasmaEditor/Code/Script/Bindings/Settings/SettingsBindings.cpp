#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
namespace pe
{
    static const std::unordered_map<std::string_view, bool GlobalSettings::*> s_boolSettings = {
        {"shadows", &GlobalSettings::shadows},
        {"ssao", &GlobalSettings::ssao},
        {"fxaa", &GlobalSettings::fxaa},
        {"taa", &GlobalSettings::taa},
        {"ssr", &GlobalSettings::ssr},
        {"dof", &GlobalSettings::dof},
        {"bloom", &GlobalSettings::bloom},
        {"motion_blur", &GlobalSettings::motion_blur},
        {"tonemapping", &GlobalSettings::tonemapping},
        {"IBL", &GlobalSettings::IBL},
        {"cas_sharpening", &GlobalSettings::cas_sharpening},
        {"draw_grid", &GlobalSettings::draw_grid},
        {"draw_aabbs", &GlobalSettings::draw_aabbs},
        {"day", &GlobalSettings::day},
        {"frustum_culling", &GlobalSettings::frustum_culling},
    };

    static const std::unordered_map<std::string_view, float GlobalSettings::*> s_floatSettings = {
        {"render_scale", &GlobalSettings::render_scale},
        {"cas_sharpness", &GlobalSettings::cas_sharpness},
        {"dof_focus_scale", &GlobalSettings::dof_focus_scale},
        {"dof_blur_range", &GlobalSettings::dof_blur_range},
        {"bloom_strength", &GlobalSettings::bloom_strength},
        {"bloom_range", &GlobalSettings::bloom_range},
        {"motion_blur_strength", &GlobalSettings::motion_blur_strength},
        {"IBL_intensity", &GlobalSettings::IBL_intensity},
        {"lights_intensity", &GlobalSettings::lights_intensity},
        {"time_scale", &GlobalSettings::time_scale},
    };

    static struct SettingsBindings
    {
        SettingsBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                       {
                sol::table settings_table = lua.create_named_table("settings");

                settings_table.set_function("get", [&lua](const std::string &name) -> sol::object {
                    auto &gs = Settings::Get<GlobalSettings>();
                    auto bIt = s_boolSettings.find(std::string_view(name));
                    if (bIt != s_boolSettings.end())
                        return sol::make_object(lua, gs.*(bIt->second));
                    auto fIt = s_floatSettings.find(std::string_view(name));
                    if (fIt != s_floatSettings.end())
                        return sol::make_object(lua, gs.*(fIt->second));
                    return sol::nil;
                });

                settings_table.set_function("set", [](const std::string &name, sol::object value) {
                    auto &gs = Settings::Get<GlobalSettings>();
                    if (value.is<bool>())
                    {
                        auto it = s_boolSettings.find(std::string_view(name));
                        if (it != s_boolSettings.end())
                            gs.*(it->second) = value.as<bool>();
                    }
                    else if (value.is<float>() || value.is<double>())
                    {
                        auto it = s_floatSettings.find(std::string_view(name));
                        if (it != s_floatSettings.end())
                            gs.*(it->second) = value.as<float>();
                    }
                }); });
        }
    } s_settingsBindings;
} // namespace pe
#endif
