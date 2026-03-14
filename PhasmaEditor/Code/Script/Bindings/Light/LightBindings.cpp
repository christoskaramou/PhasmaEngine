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
                }); });
        }
    } s_lightBindings;
} // namespace pe
#endif
