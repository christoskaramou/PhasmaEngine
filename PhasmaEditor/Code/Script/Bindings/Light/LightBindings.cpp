#include "Script/ScriptSystem.h"
#include "Systems/RendererSystem.h"

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
                    GetGlobalSystem<RendererSystem>()->GetScene().CreatePointLight();
                });
                lights_table.set_function("add_directional", []() {
                    GetGlobalSystem<RendererSystem>()->GetScene().CreateDirectionalLight();
                });
                lights_table.set_function("add_spot", []() {
                    GetGlobalSystem<RendererSystem>()->GetScene().CreateSpotLight();
                });
                lights_table.set_function("add_area", []() {
                    GetGlobalSystem<RendererSystem>()->GetScene().CreateAreaLight();
                });

                // --- Removal ---
                lights_table.set_function("remove_point", [](int index) {
                    auto &scene = GetGlobalSystem<RendererSystem>()->GetScene();
                    auto &v = scene.GetPointLights();
                    if (index >= 0 && index < static_cast<int>(v.size()))
                        v.erase(v.begin() + index);
                });
                lights_table.set_function("remove_directional", [](int index) {
                    auto &scene = GetGlobalSystem<RendererSystem>()->GetScene();
                    auto &v = scene.GetDirectionalLights();
                    if (index >= 0 && index < static_cast<int>(v.size()))
                        v.erase(v.begin() + index);
                });
                lights_table.set_function("remove_spot", [](int index) {
                    auto &scene = GetGlobalSystem<RendererSystem>()->GetScene();
                    auto &v = scene.GetSpotLights();
                    if (index >= 0 && index < static_cast<int>(v.size()))
                        v.erase(v.begin() + index);
                });
                lights_table.set_function("remove_area", [](int index) {
                    auto &scene = GetGlobalSystem<RendererSystem>()->GetScene();
                    auto &v = scene.GetAreaLights();
                    if (index >= 0 && index < static_cast<int>(v.size()))
                        v.erase(v.begin() + index);
                });

                // --- Name-based lookup ---
                lights_table.set_function("find", [&lua](const std::string &name) -> sol::as_table_t<std::vector<sol::object>> {
                    std::vector<sol::object> result;
                    auto &scene = GetGlobalSystem<RendererSystem>()->GetScene();

                    std::string q = name;
                    for (auto &c : q) c = static_cast<char>(std::tolower(c));

                    auto search = [&](auto &lights, const char *type) {
                        for (size_t i = 0; i < lights.size(); i++)
                        {
                            std::string n = lights[i].name;
                            for (auto &c : n) c = static_cast<char>(std::tolower(c));
                            if (n.find(q) != std::string::npos)
                            {
                                sol::table t = lua.create_table();
                                t["type"] = type;
                                t["index"] = static_cast<int>(i);
                                t["name"] = lights[i].name;
                                result.push_back(t);
                            }
                        }
                    };

                    search(scene.GetPointLights(), "point");
                    search(scene.GetDirectionalLights(), "directional");
                    search(scene.GetSpotLights(), "spot");
                    search(scene.GetAreaLights(), "area");
                    return sol::as_table(std::move(result));
                });

                // --- Point lights ---
                lights_table.set_function("get_point_lights", [&lua]() -> sol::as_table_t<std::vector<sol::table>> {
                    std::vector<sol::table> result;
                    auto &pls = GetGlobalSystem<RendererSystem>()->GetScene().GetPointLights();
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
                    auto &pls = GetGlobalSystem<RendererSystem>()->GetScene().GetPointLights();
                    if (index < 0 || index >= static_cast<int>(pls.size())) return;
                    pls[index].position = vec4(pos, radius);
                    pls[index].color = vec4(color, intensity);
                });

                // --- Directional lights ---
                lights_table.set_function("get_directional_lights", [&lua]() -> sol::as_table_t<std::vector<sol::table>> {
                    std::vector<sol::table> result;
                    auto &dls = GetGlobalSystem<RendererSystem>()->GetScene().GetDirectionalLights();
                    for (size_t i = 0; i < dls.size(); i++)
                    {
                        sol::table t = lua.create_table();
                        t["index"] = i;
                        t["name"] = dls[i].name;
                        t["color"] = vec3(dls[i].color);
                        t["intensity"] = dls[i].color.w;
                        t["position"] = vec3(dls[i].position);
                        t["rotation"] = vec4(dls[i].rotation);
                        result.push_back(t);
                    }
                    return sol::as_table(std::move(result));
                });

                lights_table.set_function("set_directional_light", [](int index, const vec3 &pos, const vec3 &color, float intensity) {
                    auto &dls = GetGlobalSystem<RendererSystem>()->GetScene().GetDirectionalLights();
                    if (index < 0 || index >= static_cast<int>(dls.size())) return;
                    dls[index].position = vec4(pos, 0.0f);
                    dls[index].color = vec4(color, intensity);
                });

                // --- Spot lights ---
                lights_table.set_function("get_spot_lights", [&lua]() -> sol::as_table_t<std::vector<sol::table>> {
                    std::vector<sol::table> result;
                    auto &sls = GetGlobalSystem<RendererSystem>()->GetScene().GetSpotLights();
                    for (size_t i = 0; i < sls.size(); i++)
                    {
                        sol::table t = lua.create_table();
                        t["index"] = i;
                        t["name"] = sls[i].name;
                        t["color"] = vec3(sls[i].color);
                        t["intensity"] = sls[i].color.w;
                        t["position"] = vec3(sls[i].position);
                        t["range"] = sls[i].position.w;
                        t["rotation"] = vec4(sls[i].rotation);
                        t["angle"] = sls[i].params.x;
                        t["falloff"] = sls[i].params.y;
                        result.push_back(t);
                    }
                    return sol::as_table(std::move(result));
                });

                lights_table.set_function("set_spot_light", [](int index, const vec3 &pos, const vec3 &color, float intensity, float range, float angle, float falloff) {
                    auto &sls = GetGlobalSystem<RendererSystem>()->GetScene().GetSpotLights();
                    if (index < 0 || index >= static_cast<int>(sls.size())) return;
                    sls[index].position = vec4(pos, range);
                    sls[index].color = vec4(color, intensity);
                    sls[index].params.x = angle;
                    sls[index].params.y = falloff;
                });

                // --- Area lights ---
                lights_table.set_function("get_area_lights", [&lua]() -> sol::as_table_t<std::vector<sol::table>> {
                    std::vector<sol::table> result;
                    auto &als = GetGlobalSystem<RendererSystem>()->GetScene().GetAreaLights();
                    for (size_t i = 0; i < als.size(); i++)
                    {
                        sol::table t = lua.create_table();
                        t["index"] = i;
                        t["name"] = als[i].name;
                        t["color"] = vec3(als[i].color);
                        t["intensity"] = als[i].color.w;
                        t["position"] = vec3(als[i].position);
                        t["range"] = als[i].position.w;
                        t["rotation"] = vec4(als[i].rotation);
                        t["width"] = als[i].size.x;
                        t["height"] = als[i].size.y;
                        result.push_back(t);
                    }
                    return sol::as_table(std::move(result));
                });

                lights_table.set_function("set_area_light", [](int index, const vec3 &pos, const vec3 &color, float intensity, float range, float width, float height) {
                    auto &als = GetGlobalSystem<RendererSystem>()->GetScene().GetAreaLights();
                    if (index < 0 || index >= static_cast<int>(als.size())) return;
                    als[index].position = vec4(pos, range);
                    als[index].color = vec4(color, intensity);
                    als[index].size.x = width;
                    als[index].size.y = height;
                });

                // --- Individual property setter (all light types) ---
                lights_table.set_function("set_property", [](const std::string &type, int index, const std::string &prop, sol::object value) {
                    auto &scene = GetGlobalSystem<RendererSystem>()->GetScene();

                    auto applyCommon = [&](auto &light) {
                        if (prop == "name") { light.name = value.as<std::string>(); return true; }
                        if (prop == "color") { vec3 c = value.as<vec3>(); light.color = vec4(c, light.color.w); return true; }
                        if (prop == "intensity") { light.color.w = value.as<float>(); return true; }
                        if (prop == "position") { vec3 p = value.as<vec3>(); light.position = vec4(p, light.position.w); return true; }
                        return false;
                    };

                    if (type == "point")
                    {
                        auto &v = scene.GetPointLights();
                        if (index < 0 || index >= static_cast<int>(v.size())) return;
                        if (applyCommon(v[index])) return;
                        if (prop == "radius") v[index].position.w = value.as<float>();
                    }
                    else if (type == "directional")
                    {
                        auto &v = scene.GetDirectionalLights();
                        if (index < 0 || index >= static_cast<int>(v.size())) return;
                        if (applyCommon(v[index])) return;
                        if (prop == "rotation") v[index].rotation = value.as<vec4>();
                    }
                    else if (type == "spot")
                    {
                        auto &v = scene.GetSpotLights();
                        if (index < 0 || index >= static_cast<int>(v.size())) return;
                        if (applyCommon(v[index])) return;
                        if (prop == "range") v[index].position.w = value.as<float>();
                        else if (prop == "rotation") v[index].rotation = value.as<vec4>();
                        else if (prop == "angle") v[index].params.x = value.as<float>();
                        else if (prop == "falloff") v[index].params.y = value.as<float>();
                    }
                    else if (type == "area")
                    {
                        auto &v = scene.GetAreaLights();
                        if (index < 0 || index >= static_cast<int>(v.size())) return;
                        if (applyCommon(v[index])) return;
                        if (prop == "range") v[index].position.w = value.as<float>();
                        else if (prop == "rotation") v[index].rotation = value.as<vec4>();
                        else if (prop == "width") v[index].size.x = value.as<float>();
                        else if (prop == "height") v[index].size.y = value.as<float>();
                    }
                });

                // --- Set rotation for directional/spot/area ---
                lights_table.set_function("set_rotation", [](const std::string &type, int index, const vec4 &rot) {
                    auto &scene = GetGlobalSystem<RendererSystem>()->GetScene();

                    if (type == "directional")
                    {
                        auto &v = scene.GetDirectionalLights();
                        if (index >= 0 && index < static_cast<int>(v.size())) v[index].rotation = rot;
                    }
                    else if (type == "spot")
                    {
                        auto &v = scene.GetSpotLights();
                        if (index >= 0 && index < static_cast<int>(v.size())) v[index].rotation = rot;
                    }
                    else if (type == "area")
                    {
                        auto &v = scene.GetAreaLights();
                        if (index >= 0 && index < static_cast<int>(v.size())) v[index].rotation = rot;
                    }
                });

                // --- Counts ---
                lights_table.set_function("get_counts", [&lua]() -> sol::table {
                    sol::table t = lua.create_table();
                    auto &scene = GetGlobalSystem<RendererSystem>()->GetScene();
                    t["point"] = scene.GetPointLights().size();
                    t["directional"] = scene.GetDirectionalLights().size();
                    t["spot"] = scene.GetSpotLights().size();
                    t["area"] = scene.GetAreaLights().size();
                    return t;
                }); });
        }
    } s_lightBindings;
} // namespace pe
