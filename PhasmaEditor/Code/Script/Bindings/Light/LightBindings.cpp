#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Systems/LightSystem.h"

namespace pe
{
    static struct LightBindings
    {
        LightBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table lights_table = lua.create_named_table("lights");

                // --- Creation ---
                lights_table.set_function("add_point", []() {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (ls) ls->CreatePointLight();
                });
                lights_table.set_function("add_directional", []() {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (ls) ls->CreateDirectionalLight();
                });
                lights_table.set_function("add_spot", []() {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (ls) ls->CreateSpotLight();
                });
                lights_table.set_function("add_area", []() {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (ls) ls->CreateAreaLight();
                });

                // --- Removal ---
                lights_table.set_function("remove_point", [](int index) {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return;
                    auto &v = ls->GetPointLights();
                    if (index >= 0 && index < static_cast<int>(v.size()))
                        v.erase(v.begin() + index);
                });
                lights_table.set_function("remove_directional", [](int index) {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return;
                    auto &v = ls->GetDirectionalLights();
                    if (index >= 0 && index < static_cast<int>(v.size()))
                        v.erase(v.begin() + index);
                });
                lights_table.set_function("remove_spot", [](int index) {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return;
                    auto &v = ls->GetSpotLights();
                    if (index >= 0 && index < static_cast<int>(v.size()))
                        v.erase(v.begin() + index);
                });
                lights_table.set_function("remove_area", [](int index) {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return;
                    auto &v = ls->GetAreaLights();
                    if (index >= 0 && index < static_cast<int>(v.size()))
                        v.erase(v.begin() + index);
                });

                // --- Point lights ---
                lights_table.set_function("get_point_lights", [&lua]() -> sol::as_table_t<std::vector<sol::table>> {
                    std::vector<sol::table> result;
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return sol::as_table(std::move(result));

                    auto &pls = ls->GetPointLights();
                    for (size_t i = 0; i < pls.size(); i++)
                    {
                        sol::table t = lua.create_table();
                        t["index"] = i;
                        t["name"] = pls[i].name;
                        t["color"] = vec3(pls[i].color);
                        t["intensity"] = pls[i].color.w;
                        t["position"] = vec3(pls[i].position);
                        t["radius"] = pls[i].position.w;
                        result.push_back(t);
                    }
                    return sol::as_table(std::move(result));
                });

                lights_table.set_function("set_point_light", [](int index, const vec3 &pos, const vec3 &color, float intensity, float radius) {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return;
                    auto &pls = ls->GetPointLights();
                    if (index < 0 || index >= static_cast<int>(pls.size())) return;
                    pls[index].position = vec4(pos, radius);
                    pls[index].color = vec4(color, intensity);
                });

                // --- Directional lights ---
                lights_table.set_function("get_directional_lights", [&lua]() -> sol::as_table_t<std::vector<sol::table>> {
                    std::vector<sol::table> result;
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return sol::as_table(std::move(result));

                    auto &dls = ls->GetDirectionalLights();
                    for (size_t i = 0; i < dls.size(); i++)
                    {
                        sol::table t = lua.create_table();
                        t["index"] = i;
                        t["name"] = dls[i].name;
                        t["color"] = vec3(dls[i].color);
                        t["intensity"] = dls[i].color.w;
                        t["position"] = vec3(dls[i].position);
                        result.push_back(t);
                    }
                    return sol::as_table(std::move(result));
                });

                lights_table.set_function("set_directional_light", [](int index, const vec3 &pos, const vec3 &color, float intensity) {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return;
                    auto &dls = ls->GetDirectionalLights();
                    if (index < 0 || index >= static_cast<int>(dls.size())) return;
                    dls[index].position = vec4(pos, 0.0f);
                    dls[index].color = vec4(color, intensity);
                });

                // --- Spot lights ---
                lights_table.set_function("get_spot_lights", [&lua]() -> sol::as_table_t<std::vector<sol::table>> {
                    std::vector<sol::table> result;
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return sol::as_table(std::move(result));

                    auto &sls = ls->GetSpotLights();
                    for (size_t i = 0; i < sls.size(); i++)
                    {
                        sol::table t = lua.create_table();
                        t["index"] = i;
                        t["name"] = sls[i].name;
                        t["color"] = vec3(sls[i].color);
                        t["intensity"] = sls[i].color.w;
                        t["position"] = vec3(sls[i].position);
                        t["range"] = sls[i].position.w;
                        t["angle"] = sls[i].params.x;
                        t["falloff"] = sls[i].params.y;
                        result.push_back(t);
                    }
                    return sol::as_table(std::move(result));
                });

                lights_table.set_function("set_spot_light", [](int index, const vec3 &pos, const vec3 &color, float intensity, float range, float angle, float falloff) {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return;
                    auto &sls = ls->GetSpotLights();
                    if (index < 0 || index >= static_cast<int>(sls.size())) return;
                    sls[index].position = vec4(pos, range);
                    sls[index].color = vec4(color, intensity);
                    sls[index].params.x = angle;
                    sls[index].params.y = falloff;
                });

                // --- Area lights ---
                lights_table.set_function("get_area_lights", [&lua]() -> sol::as_table_t<std::vector<sol::table>> {
                    std::vector<sol::table> result;
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return sol::as_table(std::move(result));

                    auto &als = ls->GetAreaLights();
                    for (size_t i = 0; i < als.size(); i++)
                    {
                        sol::table t = lua.create_table();
                        t["index"] = i;
                        t["name"] = als[i].name;
                        t["color"] = vec3(als[i].color);
                        t["intensity"] = als[i].color.w;
                        t["position"] = vec3(als[i].position);
                        t["range"] = als[i].position.w;
                        t["width"] = als[i].size.x;
                        t["height"] = als[i].size.y;
                        result.push_back(t);
                    }
                    return sol::as_table(std::move(result));
                });

                lights_table.set_function("set_area_light", [](int index, const vec3 &pos, const vec3 &color, float intensity, float range, float width, float height) {
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return;
                    auto &als = ls->GetAreaLights();
                    if (index < 0 || index >= static_cast<int>(als.size())) return;
                    als[index].position = vec4(pos, range);
                    als[index].color = vec4(color, intensity);
                    als[index].size.x = width;
                    als[index].size.y = height;
                });

                // --- Counts ---
                lights_table.set_function("get_counts", [&lua]() -> sol::table {
                    sol::table t = lua.create_table();
                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (!ls) return t;
                    t["point"] = ls->GetPointLights().size();
                    t["directional"] = ls->GetDirectionalLights().size();
                    t["spot"] = ls->GetSpotLights().size();
                    t["area"] = ls->GetAreaLights().size();
                    return t;
                }); });
        }
    } s_lightBindings;
} // namespace pe
#endif
