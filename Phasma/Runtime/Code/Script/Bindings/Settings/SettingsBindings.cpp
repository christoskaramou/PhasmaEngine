#include "Script/ScriptSystem.h"
#include "API/RHI.h"

namespace pe
{
    static const std::unordered_map<std::string_view, bool GlobalSettings::*> s_boolSettings = {
        {"shadows", &GlobalSettings::shadows},
        {"ssao", &GlobalSettings::ssao},
        {"forward_plus", &GlobalSettings::forward_plus},
        {"fxaa", &GlobalSettings::fxaa},
        {"taa", &GlobalSettings::taa},
        {"ssr", &GlobalSettings::ssr},
        {"dof", &GlobalSettings::dof},
        {"bloom", &GlobalSettings::bloom},
        {"motion_blur", &GlobalSettings::motion_blur},
        {"tonemapping", &GlobalSettings::tonemapping},
        {"color_grading", &GlobalSettings::color_grading},
        {"IBL", &GlobalSettings::IBL},
        {"cas_sharpening", &GlobalSettings::cas_sharpening},
        {"draw_grid", &GlobalSettings::draw_grid},
        {"draw_aabbs", &GlobalSettings::draw_aabbs},
        {"frustum_culling", &GlobalSettings::frustum_culling},
        {"randomize_lights", &GlobalSettings::randomize_lights},
        {"physical_point_falloff", &GlobalSettings::physical_point_falloff},
        {"use_Disney_PBR", &GlobalSettings::use_Disney_PBR},
        {"freeze_frustum_culling", &GlobalSettings::freeze_frustum_culling},
        {"aabbs_depth_aware", &GlobalSettings::aabbs_depth_aware},
        {"dynamic_rendering", &GlobalSettings::dynamic_rendering},
    };

    static const std::unordered_map<std::string_view, float GlobalSettings::*> s_floatSettings = {
        {"render_scale", &GlobalSettings::render_scale},
        {"cas_sharpness", &GlobalSettings::cas_sharpness},
        {"dof_focus_scale", &GlobalSettings::dof_focus_scale},
        {"dof_blur_range", &GlobalSettings::dof_blur_range},
        {"bloom_strength", &GlobalSettings::bloom_strength},
        {"bloom_range", &GlobalSettings::bloom_range},
        {"motion_blur_strength", &GlobalSettings::motion_blur_strength},
        {"color_grading_lift_r", &GlobalSettings::color_grading_lift_r},
        {"color_grading_lift_g", &GlobalSettings::color_grading_lift_g},
        {"color_grading_lift_b", &GlobalSettings::color_grading_lift_b},
        {"color_grading_gamma_r", &GlobalSettings::color_grading_gamma_r},
        {"color_grading_gamma_g", &GlobalSettings::color_grading_gamma_g},
        {"color_grading_gamma_b", &GlobalSettings::color_grading_gamma_b},
        {"color_grading_gain_r", &GlobalSettings::color_grading_gain_r},
        {"color_grading_gain_g", &GlobalSettings::color_grading_gain_g},
        {"color_grading_gain_b", &GlobalSettings::color_grading_gain_b},
        {"color_grading_saturation", &GlobalSettings::color_grading_saturation},
        {"color_grading_contrast", &GlobalSettings::color_grading_contrast},
        {"color_grading_intensity", &GlobalSettings::color_grading_intensity},
        {"IBL_intensity", &GlobalSettings::IBL_intensity},
        {"lights_intensity", &GlobalSettings::lights_intensity},
        {"time_scale", &GlobalSettings::time_scale},
        {"shadow_distance", &GlobalSettings::shadow_distance},
        {"shadow_cascade_lambda", &GlobalSettings::shadow_cascade_lambda},
        {"shadow_normal_bias", &GlobalSettings::shadow_normal_bias},
        {"shadow_fade_fraction", &GlobalSettings::shadow_fade_fraction},
        {"shadow_filter_radius", &GlobalSettings::shadow_filter_radius},
    };

    static const std::unordered_map<std::string_view, int GlobalSettings::*> s_intSettings = {
        {"motion_blur_samples", &GlobalSettings::motion_blur_samples},
        {"shadow_debug_mode", &GlobalSettings::shadow_debug_mode},
    };

    static const std::unordered_map<std::string_view, uint32_t GlobalSettings::*> s_uint32Settings = {
        {"shadow_map_size", &GlobalSettings::shadow_map_size},
        {"num_cascades", &GlobalSettings::num_cascades},
    };

    static const std::unordered_map<std::string_view, RenderMode> s_renderModeMap = {
        {"raster", RenderMode::Raster},
        {"hybrid", RenderMode::Hybrid},
        {"ray_tracing", RenderMode::RayTracing},
    };

    static struct SettingsBindings
    {
        SettingsBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table settings_table = lua.create_named_table("settings");

                settings_table.set_function("get", [](const std::string &name, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    auto &gs = Settings::Get<GlobalSettings>();
                    auto bIt = s_boolSettings.find(std::string_view(name));
                    if (bIt != s_boolSettings.end())
                        return sol::make_object(lua, gs.*(bIt->second));
                    auto fIt = s_floatSettings.find(std::string_view(name));
                    if (fIt != s_floatSettings.end())
                        return sol::make_object(lua, gs.*(fIt->second));
                    auto uIt = s_uint32Settings.find(std::string_view(name));
                    if (uIt != s_uint32Settings.end())
                        return sol::make_object(lua, gs.*(uIt->second));
                    auto iIt = s_intSettings.find(std::string_view(name));
                    if (iIt != s_intSettings.end())
                        return sol::make_object(lua, gs.*(iIt->second));
                    PE_WARN("[Lua] settings.get: unknown setting '%s'", name.c_str());
                    return sol::nil;
                });

                settings_table.set_function("set", [](const std::string &name, sol::object value) {
                    auto &gs = Settings::Get<GlobalSettings>();
                    if (value.is<bool>())
                    {
                        auto it = s_boolSettings.find(std::string_view(name));
                        if (it != s_boolSettings.end())
                        {
                            gs.*(it->second) = value.as<bool>();
                        }
                    }
                    else if (value.is<float>() || value.is<double>())
                    {
                        auto fIt = s_floatSettings.find(std::string_view(name));
                        if (fIt != s_floatSettings.end())
                        {
                            gs.*(fIt->second) = value.as<float>();
                            return;
                        }
                        auto uIt = s_uint32Settings.find(std::string_view(name));
                        if (uIt != s_uint32Settings.end())
                        {
                            gs.*(uIt->second) = static_cast<uint32_t>(value.as<double>());
                            return;
                        }
                        auto iIt = s_intSettings.find(std::string_view(name));
                        if (iIt != s_intSettings.end())
                            gs.*(iIt->second) = static_cast<int>(value.as<double>());
                    }
                });

                settings_table.set_function("get_render_mode", [](void) -> std::string {
                    auto &gs = Settings::Get<GlobalSettings>();
                    switch (gs.render_mode)
                    {
                    case RenderMode::Raster: return "raster";
                    case RenderMode::Hybrid: return "hybrid";
                    case RenderMode::RayTracing: return "ray_tracing";
                    default: return "hybrid";
                    }
                });

                settings_table.set_function("set_render_mode", [](const std::string &mode) {
                    auto it = s_renderModeMap.find(std::string_view(mode));
                    if (it != s_renderModeMap.end())
                        Settings::Get<GlobalSettings>().render_mode =
                            ClampRenderModeToRayTracingSupport(it->second, RHII.GetCaps().rayTracing);
                });

                settings_table.set_function("is_ray_tracing_supported", []() -> bool {
                    return RHII.GetCaps().rayTracing;
                });

                settings_table.set_function("get_depth_bias", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    auto &gs = Settings::Get<GlobalSettings>();
                    sol::table t = lua.create_table();
                    t[1] = gs.depth_bias[0];
                    t[2] = gs.depth_bias[1];
                    t[3] = gs.depth_bias[2];
                    return t;
                });

                settings_table.set_function("set_depth_bias", [](float a, float b, float c) {
                    auto &gs = Settings::Get<GlobalSettings>();
                    gs.depth_bias = {a, b, c};
                }); });
        }
    } s_settingsBindings;
} // namespace pe
