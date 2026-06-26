#include "Script/ScriptSystem.h"
#include "API/RHI.h"

namespace pe
{
    static const std::unordered_map<std::string_view, bool SceneSettings::*> s_boolSettings = {
        {"shadows", &SceneSettings::shadows},
        {"ssao", &SceneSettings::ssao},
        {"forward_plus", &SceneSettings::forward_plus},
        {"fxaa", &SceneSettings::fxaa},
        {"taa", &SceneSettings::taa},
        {"ssr", &SceneSettings::ssr},
        {"dof", &SceneSettings::dof},
        {"bloom", &SceneSettings::bloom},
        {"motion_blur", &SceneSettings::motion_blur},
        {"tonemapping", &SceneSettings::tonemapping},
        {"color_grading", &SceneSettings::color_grading},
        {"IBL", &SceneSettings::IBL},
        {"cas_sharpening", &SceneSettings::cas_sharpening},
        {"draw_grid", &SceneSettings::draw_grid},
        {"draw_aabbs", &SceneSettings::draw_aabbs},
        {"frustum_culling", &SceneSettings::frustum_culling},
        {"occlusion_culling", &SceneSettings::occlusion_culling},
        {"lod_enabled", &SceneSettings::lod_enabled},
        {"randomize_lights", &SceneSettings::randomize_lights},
        {"physical_point_falloff", &SceneSettings::physical_point_falloff},
        {"use_Disney_PBR", &SceneSettings::use_Disney_PBR},
        {"freeze_frustum_culling", &SceneSettings::freeze_frustum_culling},
        {"aabbs_depth_aware", &SceneSettings::aabbs_depth_aware},
        {"selection_outline", &SceneSettings::selection_outline},
        {"dynamic_rendering", &SceneSettings::dynamic_rendering},
    };

    static const std::unordered_map<std::string_view, float SceneSettings::*> s_floatSettings = {
        {"render_scale", &SceneSettings::render_scale},
        {"selection_outline_color_r", &SceneSettings::selection_outline_color_r},
        {"selection_outline_color_g", &SceneSettings::selection_outline_color_g},
        {"selection_outline_color_b", &SceneSettings::selection_outline_color_b},
        {"selection_outline_color_a", &SceneSettings::selection_outline_color_a},
        {"selection_outline_thickness", &SceneSettings::selection_outline_thickness},
        {"selection_outline_inner_fade", &SceneSettings::selection_outline_inner_fade},
        {"selection_outline_outer_fade", &SceneSettings::selection_outline_outer_fade},
        {"ssao_radius", &SceneSettings::ssao_radius},
        {"ssao_bias", &SceneSettings::ssao_bias},
        {"ssao_intensity", &SceneSettings::ssao_intensity},
        {"ssao_power", &SceneSettings::ssao_power},
        {"occlusion_culling_bias", &SceneSettings::occlusion_culling_bias},
        {"lod_bias", &SceneSettings::lod_bias},
        {"cas_sharpness", &SceneSettings::cas_sharpness},
        {"dof_focus_scale", &SceneSettings::dof_focus_scale},
        {"dof_blur_range", &SceneSettings::dof_blur_range},
        {"bloom_strength", &SceneSettings::bloom_strength},
        {"bloom_range", &SceneSettings::bloom_range},
        {"motion_blur_strength", &SceneSettings::motion_blur_strength},
        {"color_grading_lift_r", &SceneSettings::color_grading_lift_r},
        {"color_grading_lift_g", &SceneSettings::color_grading_lift_g},
        {"color_grading_lift_b", &SceneSettings::color_grading_lift_b},
        {"color_grading_gamma_r", &SceneSettings::color_grading_gamma_r},
        {"color_grading_gamma_g", &SceneSettings::color_grading_gamma_g},
        {"color_grading_gamma_b", &SceneSettings::color_grading_gamma_b},
        {"color_grading_gain_r", &SceneSettings::color_grading_gain_r},
        {"color_grading_gain_g", &SceneSettings::color_grading_gain_g},
        {"color_grading_gain_b", &SceneSettings::color_grading_gain_b},
        {"color_grading_saturation", &SceneSettings::color_grading_saturation},
        {"color_grading_contrast", &SceneSettings::color_grading_contrast},
        {"color_grading_intensity", &SceneSettings::color_grading_intensity},
        {"IBL_intensity", &SceneSettings::IBL_intensity},
        {"lights_intensity", &SceneSettings::lights_intensity},
        {"time_scale", &SceneSettings::time_scale},
        {"shadow_distance", &SceneSettings::shadow_distance},
        {"shadow_cascade_lambda", &SceneSettings::shadow_cascade_lambda},
        {"shadow_normal_bias", &SceneSettings::shadow_normal_bias},
        {"shadow_fade_fraction", &SceneSettings::shadow_fade_fraction},
        {"shadow_filter_radius", &SceneSettings::shadow_filter_radius},
    };

    static const std::unordered_map<std::string_view, int SceneSettings::*> s_intSettings = {
        {"motion_blur_samples", &SceneSettings::motion_blur_samples},
        {"ssao_samples", &SceneSettings::ssao_samples},
        {"shadow_debug_mode", &SceneSettings::shadow_debug_mode},
    };

    static const std::unordered_map<std::string_view, uint32_t SceneSettings::*> s_uint32Settings = {
        {"shadow_map_size", &SceneSettings::shadow_map_size},
        {"num_cascades", &SceneSettings::num_cascades},
        {"lod_count", &SceneSettings::lod_count},
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
                    auto &gs = Settings::Get<SceneSettings>();
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
                    auto &gs = Settings::Get<SceneSettings>();
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
                    auto &gs = Settings::Get<SceneSettings>();
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
                        Settings::Get<SceneSettings>().render_mode =
                            ClampRenderModeToRayTracingSupport(it->second, RHII.GetCaps().rayTracing);
                });

                settings_table.set_function("is_ray_tracing_supported", []() -> bool {
                    return RHII.GetCaps().rayTracing;
                });

                settings_table.set_function("get_depth_bias", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    auto &gs = Settings::Get<SceneSettings>();
                    sol::table t = lua.create_table();
                    t[1] = gs.depth_bias[0];
                    t[2] = gs.depth_bias[1];
                    t[3] = gs.depth_bias[2];
                    return t;
                });

                settings_table.set_function("set_depth_bias", [](float a, float b, float c) {
                    auto &gs = Settings::Get<SceneSettings>();
                    gs.depth_bias = {a, b, c};
                });

                settings_table.set_function("get_lod_distances", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    auto &gs = Settings::Get<SceneSettings>();
                    sol::table t = lua.create_table();
                    t[1] = gs.lod_distances[0];
                    t[2] = gs.lod_distances[1];
                    t[3] = gs.lod_distances[2];
                    return t;
                });

                settings_table.set_function("set_lod_distances", [](float a, float b, float c) {
                    auto &gs = Settings::Get<SceneSettings>();
                    gs.lod_distances = {a, b, c};
                }); });
        }
    } s_settingsBindings;
} // namespace pe
