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

    static int ResolveMeshIdx(Scene *s, const SceneNodeHandle &h, int slot = 0)
    {
        if (!s || !h.IsValid(*s))
            return -1;
        const auto &refs = s->GetNodeCache(h.nodeId).meshRefs->meshRefs;
        if (slot < 0 || slot >= static_cast<int>(refs.size()))
            return -1;
        return refs[slot];
    }

    static struct MaterialBindings
    {
        MaterialBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table mat = lua.create_named_table("material");

                auto buildMaterialTable = [](Scene *s, int meshIdx, sol::state_view &lua) -> sol::object {
                    if (meshIdx < 0) return sol::make_object(lua, sol::nil);
                    const Mesh &mesh = s->GetMeshes()[meshIdx];
                    if (!mesh.material) return sol::make_object(lua, sol::nil);
                    // Read from instance if present, otherwise shared material
                    MaterialInstance *inst = mesh.materialInstance;
                    sol::table t = lua.create_table();
                    t["instanced"] = inst != nullptr;
                    t["base_color"] = inst ? inst->GetBaseColorFactor() : mesh.material->baseColorFactor;
                    t["emissive"] = inst ? inst->GetEmissiveFactor() : mesh.material->emissiveFactor;
                    t["transmission"] = inst ? inst->GetTransmissionFactor() : mesh.material->transmissionFactor;
                    t["metallic"] = inst ? inst->GetMetallic() : mesh.material->metallic;
                    t["roughness"] = inst ? inst->GetRoughness() : mesh.material->roughness;
                    t["alpha_cutoff"] = inst ? inst->GetAlphaCutoff() : mesh.material->alphaCutoff;
                    t["occlusion_strength"] = inst ? inst->GetOcclusionStrength() : mesh.material->occlusionStrength;
                    t["normal_scale"] = inst ? inst->GetNormalScale() : mesh.material->normalScale;
                    t["ior"] = inst ? inst->GetIor() : mesh.material->ior;
                    t["thickness_factor"] = inst ? inst->GetThicknessFactor() : mesh.material->thicknessFactor;
                    t["attenuation_distance"] = inst ? inst->GetAttenuationDistance() : mesh.material->attenuationDistance;
                    return sol::make_object(lua, t);
                };

                mat.set_function("get", sol::overload(
                    [buildMaterialTable](SceneNodeHandle &h, sol::this_state ts) -> sol::object {
                        sol::state_view lua(ts);
                        Scene *s = GetScene();
                        return buildMaterialTable(s, ResolveMeshIdx(s, h), lua);
                    },
                    [buildMaterialTable](SceneNodeHandle &h, int slot, sol::this_state ts) -> sol::object {
                        sol::state_view lua(ts);
                        Scene *s = GetScene();
                        return buildMaterialTable(s, ResolveMeshIdx(s, h, slot), lua);
                    }));

                // Ensure per-mesh instance exists, return it
                auto ensureInstance = [](Scene *s, Mesh &mesh) -> MaterialInstance * {
                    if (mesh.materialInstance) return mesh.materialInstance;
                    return s->CreateMaterialInstance(mesh);
                };

                auto setVec4 = [ensureInstance](Scene *s, NodeId *nodeId, int meshIdx, const std::string &prop, vec4 value) {
                    if (meshIdx < 0) return;
                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (!mesh.material) return;
                    MaterialInstance *inst = ensureInstance(s, mesh);
                    if (!inst) return;
                    if (prop == "base_color") inst->SetBaseColorFactor(value);
                    s->SetTexturesDirty();
                    s->SetMaterialDirty();
                    s->MarkNodeDirty(nodeId);
                };
                auto setVec3 = [ensureInstance](Scene *s, NodeId *nodeId, int meshIdx, const std::string &prop, vec3 value) {
                    if (meshIdx < 0) return;
                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (!mesh.material) return;
                    MaterialInstance *inst = ensureInstance(s, mesh);
                    if (!inst) return;
                    if (prop == "base_color") inst->SetBaseColorFactor(vec4(value, 1.f));
                    else if (prop == "emissive") inst->SetEmissiveFactor(value);
                    s->SetTexturesDirty();
                    s->SetMaterialDirty();
                    s->MarkNodeDirty(nodeId);
                };
                auto setFloat = [ensureInstance](Scene *s, NodeId *nodeId, int meshIdx, const std::string &prop, float value) {
                    if (meshIdx < 0) return;
                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (!mesh.material) return;
                    MaterialInstance *inst = ensureInstance(s, mesh);
                    if (!inst) return;
                    if (prop == "transmission") inst->SetTransmissionFactor(value);
                    else if (prop == "metallic") inst->SetMetallic(value);
                    else if (prop == "roughness") inst->SetRoughness(value);
                    else if (prop == "alpha_cutoff") inst->SetAlphaCutoff(value);
                    else if (prop == "occlusion_strength") inst->SetOcclusionStrength(value);
                    else if (prop == "normal_scale") inst->SetNormalScale(value);
                    else if (prop == "ior") inst->SetIor(value);
                    else if (prop == "thickness_factor") inst->SetThicknessFactor(value);
                    else if (prop == "attenuation_distance") inst->SetAttenuationDistance(value);
                    else return;
                    s->SetTexturesDirty();
                    s->SetMaterialDirty();
                    s->MarkNodeDirty(nodeId);
                };

                mat.set_function("set", sol::overload(
                    [setVec4](SceneNodeHandle &h, const std::string &prop, vec4 value) {
                        Scene *s = GetScene();
                        setVec4(s, h.nodeId, ResolveMeshIdx(s, h), prop, value);
                    },
                    [setVec4](SceneNodeHandle &h, int slot, const std::string &prop, vec4 value) {
                        Scene *s = GetScene();
                        setVec4(s, h.nodeId, ResolveMeshIdx(s, h, slot), prop, value);
                    },
                    [setVec3](SceneNodeHandle &h, const std::string &prop, vec3 value) {
                        Scene *s = GetScene();
                        setVec3(s, h.nodeId, ResolveMeshIdx(s, h), prop, value);
                    },
                    [setVec3](SceneNodeHandle &h, int slot, const std::string &prop, vec3 value) {
                        Scene *s = GetScene();
                        setVec3(s, h.nodeId, ResolveMeshIdx(s, h, slot), prop, value);
                    },
                    [setFloat](SceneNodeHandle &h, const std::string &prop, float value) {
                        Scene *s = GetScene();
                        setFloat(s, h.nodeId, ResolveMeshIdx(s, h), prop, value);
                    },
                    [setFloat](SceneNodeHandle &h, int slot, const std::string &prop, float value) {
                        Scene *s = GetScene();
                        setFloat(s, h.nodeId, ResolveMeshIdx(s, h, slot), prop, value);
                    }));

                auto getRenderTypeStr = [](Scene *s, int meshIdx) -> std::string {
                    if (meshIdx < 0) return "opaque";
                    switch (s->GetMeshes()[meshIdx].renderType)
                    {
                    case RenderType::Opaque: return "opaque";
                    case RenderType::AlphaCut: return "alpha_cut";
                    case RenderType::AlphaBlend: return "alpha_blend";
                    case RenderType::Transmission: return "transmission";
                    default: return "opaque";
                    }
                };

                mat.set_function("get_render_type", sol::overload(
                    [getRenderTypeStr](SceneNodeHandle &h) -> std::string {
                        Scene *s = GetScene();
                        return getRenderTypeStr(s, ResolveMeshIdx(s, h));
                    },
                    [getRenderTypeStr](SceneNodeHandle &h, int slot) -> std::string {
                        Scene *s = GetScene();
                        return getRenderTypeStr(s, ResolveMeshIdx(s, h, slot));
                    }));

                auto setRenderTypeImpl = [](Scene *s, int meshIdx, const std::string &type) {
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
                    if (mesh.materialInstance)
                        mesh.materialInstance->SetRenderType(rt);
                    s->SetGeometryDirty();
                };

                mat.set_function("set_render_type", sol::overload(
                    [setRenderTypeImpl](SceneNodeHandle &h, const std::string &type) {
                        Scene *s = GetScene();
                        setRenderTypeImpl(s, ResolveMeshIdx(s, h), type);
                    },
                    [setRenderTypeImpl](SceneNodeHandle &h, int slot, const std::string &type) {
                        Scene *s = GetScene();
                        setRenderTypeImpl(s, ResolveMeshIdx(s, h, slot), type);
                    }));

                auto getTexMask = [](Scene *s, int meshIdx) -> uint32_t {
                    if (meshIdx < 0) return 0u;
                    const Mesh &mesh = s->GetMeshes()[meshIdx];
                    if (mesh.materialInstance) return mesh.materialInstance->GetTextureMask();
                    return mesh.material ? mesh.material->textureMask : 0u;
                };

                mat.set_function("get_texture_mask", sol::overload(
                    [getTexMask](SceneNodeHandle &h) -> uint32_t {
                        Scene *s = GetScene();
                        return getTexMask(s, ResolveMeshIdx(s, h));
                    },
                    [getTexMask](SceneNodeHandle &h, int slot) -> uint32_t {
                        Scene *s = GetScene();
                        return getTexMask(s, ResolveMeshIdx(s, h, slot));
                    }));

                mat.set_function("has_texture", sol::overload(
                    [getTexMask](SceneNodeHandle &h, const std::string &type) -> bool {
                        Scene *s = GetScene();
                        int meshIdx = ResolveMeshIdx(s, h);
                        uint32_t mask = getTexMask(s, meshIdx);
                        auto it = s_texSlots.find(std::string_view(type));
                        if (it == s_texSlots.end()) return false;
                        return (mask & (1u << it->second)) != 0;
                    },
                    [getTexMask](SceneNodeHandle &h, int slot, const std::string &type) -> bool {
                        Scene *s = GetScene();
                        int meshIdx = ResolveMeshIdx(s, h, slot);
                        uint32_t mask = getTexMask(s, meshIdx);
                        auto it = s_texSlots.find(std::string_view(type));
                        if (it == s_texSlots.end()) return false;
                        return (mask & (1u << it->second)) != 0;
                    }));

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
