#include "Script/ScriptSystem.h"
#include "Particles/ParticleManager.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    static const std::unordered_map<std::string_view, uint32_t> s_orientationMap = {
        {"billboard", 0}, {"horizontal", 1}, {"vertical", 2}, {"velocity", 3}};

    static ParticleManager *GetPM()
    {
        auto *r = GetGlobalSystem<RendererSystem>();
        return r ? r->GetScene().GetParticleManager() : nullptr;
    }

    static struct ParticleBindings
    {
        ParticleBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table particles = lua.create_named_table("particles");

                particles.set_function("get_count", []() -> int {
                    auto *pm = GetPM();
                    return pm ? static_cast<int>(pm->GetEmitterCount()) : 0;
                });

                particles.set_function("get_particle_count", []() -> int {
                    auto *pm = GetPM();
                    return pm ? static_cast<int>(pm->GetParticleCount()) : 0;
                });

                particles.set_function("add_emitter", [](sol::optional<sol::table> opts) -> int {
                    auto *pm = GetPM();
                    if (!pm) return -1;

                    ParticleEmitter e{};
                    e.position = vec4(0.0f, 0.0f, 0.0f, 1.0f);
                    e.velocity = vec4(0.0f, 5.0f, 0.0f, 0.0f);
                    e.colorStart = vec4(1.0f, 1.0f, 1.0f, 1.0f);
                    e.colorEnd = vec4(0.0f, 0.0f, 0.0f, 0.0f);
                    e.sizeLife = vec4(0.05f, 0.15f, 1.0f, 2.0f);
                    e.physics = vec4(50.0f, 0.5f, 1.0f, 0.1f);
                    e.gravity = vec4(0.0f, -9.8f, 0.0f, 0.0f);
                    e.animation = vec4(1.0f, 1.0f, 1.0f, 0.0f);
                    e.textureIndex = 0;
                    e.count = 100;
                    e.offset = 0;
                    e.orientation = 0;

                    if (opts.has_value())
                    {
                        sol::table t = opts.value();
                        if (t["position"].valid()) { vec3 p = t["position"]; e.position = vec4(p, 1.0f); }
                        if (t["velocity"].valid()) { vec3 v = t["velocity"]; e.velocity = vec4(v, 0.0f); }
                        if (t["color_start"].valid()) e.colorStart = t["color_start"].get<vec4>();
                        if (t["color_end"].valid()) e.colorEnd = t["color_end"].get<vec4>();
                        if (t["gravity"].valid()) { vec3 g = t["gravity"]; e.gravity = vec4(g, 0.0f); }
                        if (t["count"].valid()) e.count = t["count"].get<uint32_t>();
                        if (t["size_min"].valid()) e.sizeLife.x = t["size_min"].get<float>();
                        if (t["size_max"].valid()) e.sizeLife.y = t["size_max"].get<float>();
                        if (t["life_min"].valid()) e.sizeLife.z = t["life_min"].get<float>();
                        if (t["life_max"].valid()) e.sizeLife.w = t["life_max"].get<float>();
                        if (t["spawn_rate"].valid()) e.physics.x = t["spawn_rate"].get<float>();
                        if (t["spawn_radius"].valid()) e.physics.y = t["spawn_radius"].get<float>();
                        if (t["noise_strength"].valid()) e.physics.z = t["noise_strength"].get<float>();
                        if (t["drag"].valid()) e.physics.w = t["drag"].get<float>();
                        if (t["texture_index"].valid()) e.textureIndex = t["texture_index"].get<uint32_t>();
                        if (t["anim_rows"].valid()) e.animation.x = t["anim_rows"].get<float>();
                        if (t["anim_cols"].valid()) e.animation.y = t["anim_cols"].get<float>();
                        if (t["anim_speed"].valid()) e.animation.z = t["anim_speed"].get<float>();
                        if (t["interpolate"].valid()) e.animation.w = t["interpolate"].get<bool>() ? 1.0f : 0.0f;
                        if (t["orientation"].valid())
                        {
                            std::string o = t["orientation"].get<std::string>();
                            auto it = s_orientationMap.find(std::string_view(o));
                            if (it != s_orientationMap.end()) e.orientation = it->second;
                        }
                    }

                    auto &emitters = pm->GetEmitters();
                    auto &names = pm->GetEmitterNames();
                    emitters.push_back(e);
                    names.push_back("Emitter " + std::to_string(emitters.size() - 1));
                    pm->UpdateEmitterBuffer();
                    return static_cast<int>(emitters.size() - 1);
                });

                particles.set_function("remove_emitter", [](int index) {
                    auto *pm = GetPM();
                    if (!pm) return;
                    auto &emitters = pm->GetEmitters();
                    auto &names = pm->GetEmitterNames();
                    if (index < 0 || index >= static_cast<int>(emitters.size())) return;
                    emitters.erase(emitters.begin() + index);
                    if (index < static_cast<int>(names.size()))
                        names.erase(names.begin() + index);
                    pm->UpdateEmitterBuffer();
                });

                particles.set_function("get_emitter", [](int index, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    auto *pm = GetPM();
                    if (!pm) return sol::nil;
                    auto &emitters = pm->GetEmitters();
                    if (index < 0 || index >= static_cast<int>(emitters.size())) return sol::nil;

                    auto &e = emitters[index];
                    auto &names = pm->GetEmitterNames();
                    sol::table t = lua.create_table();
                    t["name"] = (index < static_cast<int>(names.size())) ? names[index] : "";
                    t["position"] = vec3(e.position);
                    t["velocity"] = vec3(e.velocity);
                    t["color_start"] = e.colorStart;
                    t["color_end"] = e.colorEnd;
                    t["gravity"] = vec3(e.gravity);
                    t["count"] = e.count;
                    t["size_min"] = e.sizeLife.x;
                    t["size_max"] = e.sizeLife.y;
                    t["life_min"] = e.sizeLife.z;
                    t["life_max"] = e.sizeLife.w;
                    t["spawn_rate"] = e.physics.x;
                    t["spawn_radius"] = e.physics.y;
                    t["noise_strength"] = e.physics.z;
                    t["drag"] = e.physics.w;
                    t["orientation"] = e.orientation;
                    t["texture_index"] = e.textureIndex;
                    t["anim_rows"] = e.animation.x;
                    t["anim_cols"] = e.animation.y;
                    t["anim_speed"] = e.animation.z;
                    t["interpolate"] = e.animation.w > 0.5f;
                    return t;
                });

                particles.set_function("set_emitter", [](int index, sol::object propOrTable, sol::optional<sol::object> value) {
                    auto *pm = GetPM();
                    if (!pm) return;
                    auto &emitters = pm->GetEmitters();
                    if (index < 0 || index >= static_cast<int>(emitters.size())) return;

                    auto &e = emitters[index];

                    auto applyProp = [&](const std::string &prop, sol::object val) {
                        if (prop == "position") { vec3 p = val.as<vec3>(); e.position = vec4(p, 1.0f); }
                        else if (prop == "velocity") { vec3 v = val.as<vec3>(); e.velocity = vec4(v, 0.0f); }
                        else if (prop == "color_start") e.colorStart = val.as<vec4>();
                        else if (prop == "color_end") e.colorEnd = val.as<vec4>();
                        else if (prop == "gravity") { vec3 g = val.as<vec3>(); e.gravity = vec4(g, 0.0f); }
                        else if (prop == "count") e.count = static_cast<uint32_t>(val.as<double>());
                        else if (prop == "size_min") e.sizeLife.x = val.as<float>();
                        else if (prop == "size_max") e.sizeLife.y = val.as<float>();
                        else if (prop == "life_min") e.sizeLife.z = val.as<float>();
                        else if (prop == "life_max") e.sizeLife.w = val.as<float>();
                        else if (prop == "spawn_rate") e.physics.x = val.as<float>();
                        else if (prop == "spawn_radius") e.physics.y = val.as<float>();
                        else if (prop == "noise_strength") e.physics.z = val.as<float>();
                        else if (prop == "drag") e.physics.w = val.as<float>();
                        else if (prop == "orientation")
                        {
                            auto it = s_orientationMap.find(std::string_view(val.as<std::string>()));
                            if (it != s_orientationMap.end()) e.orientation = it->second;
                        }
                        else if (prop == "texture_index") e.textureIndex = static_cast<uint32_t>(val.as<double>());
                        else if (prop == "anim_rows") e.animation.x = val.as<float>();
                        else if (prop == "anim_cols") e.animation.y = val.as<float>();
                        else if (prop == "anim_speed") e.animation.z = val.as<float>();
                        else if (prop == "interpolate") e.animation.w = val.as<bool>() ? 1.0f : 0.0f;
                        else if (prop == "name")
                        {
                            auto &names = pm->GetEmitterNames();
                            if (index < static_cast<int>(names.size()))
                                names[index] = val.as<std::string>();
                        }
                    };

                    if (propOrTable.is<sol::table>())
                    {
                        // Table overload: set_emitter(index, {prop1=val1, prop2=val2, ...})
                        sol::table t = propOrTable.as<sol::table>();
                        for (auto &kv : t)
                        {
                            if (kv.first.is<std::string>())
                                applyProp(kv.first.as<std::string>(), kv.second);
                        }
                    }
                    else if (propOrTable.is<std::string>() && value.has_value())
                    {
                        // Single property overload: set_emitter(index, "prop", value)
                        applyProp(propOrTable.as<std::string>(), value.value());
                    }

                    pm->UpdateEmitterBuffer();
                });

                particles.set_function("load_texture", [](const std::string &path) -> int {
                    auto *pm = GetPM();
                    if (!pm) return -1;
                    return static_cast<int>(pm->LoadTexture(path));
                });

                particles.set_function("get_texture_names", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    auto *pm = GetPM();
                    sol::table t = lua.create_table();
                    if (!pm) return t;
                    const auto &names = pm->GetTextureNames();
                    for (size_t i = 0; i < names.size(); i++)
                        t[static_cast<int>(i + 1)] = names[i];
                    return t;
                });

                particles.set_function("find", [](const std::string &name) -> int {
                    auto *pm = GetPM();
                    if (!pm) return -1;
                    auto &names = pm->GetEmitterNames();

                    std::string q = name;
                    for (auto &c : q) c = static_cast<char>(std::tolower(c));

                    for (size_t i = 0; i < names.size(); i++)
                    {
                        std::string n = names[i];
                        for (auto &c : n) c = static_cast<char>(std::tolower(c));
                        if (n.find(q) != std::string::npos)
                            return static_cast<int>(i);
                    }
                    return -1;
                });

                particles.set_function("clear", []() {
                    auto *pm = GetPM();
                    if (!pm) return;
                    pm->GetEmitters().clear();
                    pm->GetEmitterNames().clear();
                    pm->UpdateEmitterBuffer();
                }); });
        }
    } s_particleBindings;
} // namespace pe
