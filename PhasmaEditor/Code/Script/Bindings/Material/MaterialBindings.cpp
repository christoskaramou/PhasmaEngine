#include "Script/ScriptSystem.h"
#include "Scene/Material.h"
#include "Scene/Scene.h"
#include "Scene/SceneNodeHandle.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    static const std::unordered_map<std::string_view, uint32_t> s_texSlots = {
        {"base_color", 0}, {"metallic_roughness", 1}, {"normal", 2}, {"occlusion", 3}, {"emissive", 4}};

    static Scene *GetScene()
    {
        auto *r = GetGlobalSystem<RendererSystem>();
        return r ? &r->GetScene() : nullptr;
    }

    static struct MaterialBindings
    {
        MaterialBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table mat = lua.create_named_table("material");

                mat.set_function("get", [](SceneNodeHandle &h, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return sol::make_object(lua, sol::nil);

                    int meshIdx = s->GetMeshRef(h.nodeId);
                    if (meshIdx < 0) return sol::make_object(lua, sol::nil);

                    const Mesh &mesh = s->GetMeshes()[meshIdx];
                    if (!mesh.material) return sol::make_object(lua, sol::nil);
                    const Material &m = *mesh.material;

                    sol::table t = lua.create_table();
                    t["base_color"] = m.baseColorFactor;
                    t["emissive"] = m.emissiveFactor;
                    t["transmission"] = m.transmissionFactor;
                    t["metallic"] = m.metallic;
                    t["roughness"] = m.roughness;
                    t["alpha_cutoff"] = m.alphaCutoff;
                    t["occlusion_strength"] = m.occlusionStrength;
                    t["normal_scale"] = m.normalScale;
                    return sol::make_object(lua, t);
                });

                mat.set_function("set", sol::overload(
                    [](SceneNodeHandle &h, const std::string &prop, vec4 value) {
                        Scene *s = GetScene();
                        if (!s || !h.IsValid(*s)) return;
                        int meshIdx = s->GetMeshRef(h.nodeId);
                        if (meshIdx < 0) return;
                        Mesh &mesh = s->GetMesh(meshIdx);
                        if (!mesh.material) return;
                        if (prop == "base_color") mesh.material->baseColorFactor = value;
                        mesh.material->dirty = true;
                        s->MarkNodeDirty(h.nodeId);
                    },
                    [](SceneNodeHandle &h, const std::string &prop, vec3 value) {
                        Scene *s = GetScene();
                        if (!s || !h.IsValid(*s)) return;
                        int meshIdx = s->GetMeshRef(h.nodeId);
                        if (meshIdx < 0) return;
                        Mesh &mesh = s->GetMesh(meshIdx);
                        if (!mesh.material) return;
                        if (prop == "base_color") mesh.material->baseColorFactor = vec4(value, 1.f);
                        else if (prop == "emissive") mesh.material->emissiveFactor = value;
                        mesh.material->dirty = true;
                        s->MarkNodeDirty(h.nodeId);
                    },
                    [](SceneNodeHandle &h, const std::string &prop, float value) {
                        Scene *s = GetScene();
                        if (!s || !h.IsValid(*s)) return;
                        int meshIdx = s->GetMeshRef(h.nodeId);
                        if (meshIdx < 0) return;
                        Mesh &mesh = s->GetMesh(meshIdx);
                        if (!mesh.material) return;
                        Material &m = *mesh.material;
                        if (prop == "transmission") m.transmissionFactor = value;
                        else if (prop == "metallic") m.metallic = value;
                        else if (prop == "roughness") m.roughness = value;
                        else if (prop == "alpha_cutoff") m.alphaCutoff = value;
                        else if (prop == "occlusion_strength") m.occlusionStrength = value;
                        else if (prop == "normal_scale") m.normalScale = value;
                        else return;
                        m.dirty = true;
                        s->MarkNodeDirty(h.nodeId);
                    }));

                mat.set_function("get_render_type", [](SceneNodeHandle &h) -> std::string {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return "opaque";
                    int meshIdx = s->GetMeshRef(h.nodeId);
                    if (meshIdx < 0) return "opaque";
                    switch (s->GetMeshes()[meshIdx].renderType)
                    {
                    case RenderType::Opaque: return "opaque";
                    case RenderType::AlphaCut: return "alpha_cut";
                    case RenderType::AlphaBlend: return "alpha_blend";
                    case RenderType::Transmission: return "transmission";
                    default: return "opaque";
                    }
                });

                mat.set_function("set_render_type", [](SceneNodeHandle &h, const std::string &type) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    int meshIdx = s->GetMeshRef(h.nodeId);
                    if (meshIdx < 0) return;

                    RenderType rt;
                    if (type == "opaque") rt = RenderType::Opaque;
                    else if (type == "alpha_cut") rt = RenderType::AlphaCut;
                    else if (type == "alpha_blend") rt = RenderType::AlphaBlend;
                    else if (type == "transmission") rt = RenderType::Transmission;
                    else return;

                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (mesh.renderType == rt) return;
                    mesh.renderType = rt;
                    s->SetGeometryDirty();
                });

                mat.set_function("get_texture_mask", [](SceneNodeHandle &h) -> uint32_t {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return 0u;
                    int meshIdx = s->GetMeshRef(h.nodeId);
                    if (meshIdx < 0) return 0u;
                    const Mesh &mesh = s->GetMeshes()[meshIdx];
                    return mesh.material ? mesh.material->textureMask : 0u;
                });

                mat.set_function("has_texture", [](SceneNodeHandle &h, const std::string &type) -> bool {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return false;
                    int meshIdx = s->GetMeshRef(h.nodeId);
                    if (meshIdx < 0) return false;
                    const Mesh &mesh = s->GetMeshes()[meshIdx];
                    if (!mesh.material) return false;
                    auto it = s_texSlots.find(std::string_view(type));
                    if (it == s_texSlots.end()) return false;
                    return (mesh.material->textureMask & (1u << it->second)) != 0;
                });

                // set_texture and remove_texture require scene-level texture loading;
                // kept as stubs that return false until scene-level LoadTexture is implemented.
                mat.set_function("set_texture", [](SceneNodeHandle &, const std::string &, const std::string &) -> bool {
                    PE_WARN("[Lua] material.set_texture: not yet implemented for SceneNode API");
                    return false;
                });

                mat.set_function("remove_texture", [](SceneNodeHandle &, const std::string &) -> bool {
                    PE_WARN("[Lua] material.remove_texture: not yet implemented for SceneNode API");
                    return false;
                }); });
        }
    } s_materialBindings;
} // namespace pe
