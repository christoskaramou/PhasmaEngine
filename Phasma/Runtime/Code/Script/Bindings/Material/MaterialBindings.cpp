#include "Script/ScriptSystem.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNode.h"
#include "Scene/SceneNodeHandle.h"

namespace pe
{
    static const std::unordered_map<std::string_view, TextureType> s_texSlots = {
        {"base_color", TextureType::BaseColor},
        {"basecolor", TextureType::BaseColor},
        {"albedo", TextureType::BaseColor},
        {"diffuse", TextureType::BaseColor},
        {"metallic_roughness", TextureType::MetallicRoughness},
        {"metallicRoughness", TextureType::MetallicRoughness},
        {"roughness", TextureType::MetallicRoughness},
        {"normal", TextureType::Normal},
        {"occlusion", TextureType::Occlusion},
        {"ao", TextureType::Occlusion},
        {"emissive", TextureType::Emissive}};

    static Scene *GetScene()
    {
        return GetActiveScene();
    }

    static int ResolveMeshIdx(Scene *s, const SceneNodeHandle &h, int slot = 0);

    static std::filesystem::path U8Path(const std::string &utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }

    static std::filesystem::path ResolveTexturePath(const std::string &path)
    {
        std::filesystem::path p = U8Path(path);
        if (p.is_absolute())
            return p;

        const std::filesystem::path assets = U8Path(Path::Assets);
        std::array<std::filesystem::path, 4> candidates = {
            assets / p,
            assets / "Textures" / p,
            assets / "Objects" / p,
            p};

        for (const auto &candidate : candidates)
        {
            if (AssetFileExists(candidate))
                return candidate;
        }

        return candidates[0];
    }

    static ResourceHandle<Image> LoadTextureForLua(const std::string &path)
    {
        Queue *queue = RHII.GetMainQueue();
        if (!queue)
            return ResourceHandle<Image>();

        const std::filesystem::path resolvedPath = ResolveTexturePath(path);
        if (!AssetFileExists(resolvedPath))
        {
            PE_WARN("[Lua] texture file not found: %s", path.c_str());
            return ResourceHandle<Image>();
        }

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        ModelAsset loader;
        ResourceHandle<Image> image = loader.LoadTexture(cmd, resolvedPath);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        queue->ReturnCommandBuffer(cmd);
        return image;
    }

    static MaterialInstance *EnsureInstance(Scene *s, Mesh &mesh)
    {
        if (mesh.materialInstance)
            return mesh.materialInstance;
        return s->CreateMaterialInstance(mesh);
    }

    static bool ApplyTexture(Scene *s, NodeId *nodeId, int meshIdx, TextureType slot, ResourceHandle<Image> image)
    {
        if (!s || !s->IsNodeAlive(nodeId) || !image || !s->IsValidMeshIndex(meshIdx))
            return false;

        Mesh &mesh = s->GetMesh(meshIdx);
        if (!mesh.material)
            return false;

        MaterialInstance *inst = EnsureInstance(s, mesh);
        if (!inst)
            return false;

        inst->SetTexture(slot, image);
        inst->SetTextureMask(inst->GetTextureMask() | TextureBit(slot));
        s->SetTexturesDirty();
        s->SetMaterialDirty();
        s->MarkNodeDirty(nodeId);
        return true;
    }

    static bool ClearTexture(Scene *s, NodeId *nodeId, int meshIdx, TextureType slot)
    {
        if (!s || !s->IsNodeAlive(nodeId) || !s->IsValidMeshIndex(meshIdx))
            return false;

        Mesh &mesh = s->GetMesh(meshIdx);
        if (!mesh.material)
            return false;

        MaterialInstance *inst = EnsureInstance(s, mesh);
        if (!inst)
            return false;

        inst->SetTexture(slot, ModelAsset::DefaultTextureForSlot(slot));
        inst->SetTextureMask(inst->GetTextureMask() & ~TextureBit(slot));
        s->SetTexturesDirty();
        s->SetMaterialDirty();
        s->MarkNodeDirty(nodeId);
        return true;
    }

    static bool SetNodeTexture(SceneNodeHandle &h, int slotIndex, const std::string &type, const std::string &path)
    {
        Scene *s = GetScene();
        auto slotIt = s_texSlots.find(std::string_view(type));
        if (!s || !h.IsValid(*s) || slotIt == s_texSlots.end())
            return false;

        ResourceHandle<Image> image = LoadTextureForLua(path);
        if (!image)
            return false;

        return ApplyTexture(s, h.nodeId, ResolveMeshIdx(s, h, slotIndex), slotIt->second, image);
    }

    static void SetNodeRenderType(Scene *s, NodeId *nodeId, int meshIdx, RenderType renderType)
    {
        if (!s || !s->IsNodeAlive(nodeId) || !s->IsValidMeshIndex(meshIdx))
            return;

        Mesh &mesh = s->GetMesh(meshIdx);
        MaterialInstance *inst = mesh.materialInstance;
        if (mesh.renderType == renderType && (!inst || inst->GetRenderType() == renderType))
            return;

        const bool routingChanged = mesh.renderType != renderType;
        mesh.renderType = renderType;
        if (mesh.material)
        {
            inst = EnsureInstance(s, mesh);
            if (inst)
                inst->SetRenderType(renderType);
        }
        if (routingChanged)
            s->SetInstancesDirty();
        s->SetMaterialDirty();
        s->MarkNodeDirty(nodeId);
    }

    static bool GetNodeDoubleSided(Scene *s, int meshIdx)
    {
        if (!s || !s->IsValidMeshIndex(meshIdx))
            return false;

        const Mesh &mesh = s->GetMeshes()[meshIdx];
        return mesh.material && mesh.material->doubleSided;
    }

    // NOTE: doubleSided lives on the SHARED Material — it drives cull / indirect-batch
    // routing, not a per-draw shader uniform, so there is no per-mesh MaterialInstance
    // fork like the vec3/vec4/float setters use. This therefore flips double-sidedness
    // for EVERY mesh that shares this material (same shared-state caveat as the
    // night_emissive setter below). Callers that need it on one mesh only must give
    // that mesh a unique material first.
    static void SetNodeDoubleSided(Scene *s, NodeId *nodeId, int meshIdx, bool doubleSided)
    {
        if (!s || !s->IsNodeAlive(nodeId) || !s->IsValidMeshIndex(meshIdx))
            return;

        Mesh &mesh = s->GetMesh(meshIdx);
        if (!mesh.material || mesh.material->doubleSided == doubleSided)
            return;

        mesh.material->doubleSided = doubleSided;
        mesh.material->dirty = true;
        s->SetInstancesDirty();
        s->SetMaterialDirty();
        s->MarkNodeDirty(nodeId);
    }

    static bool ParseRenderType(std::string_view type, RenderType &out)
    {
        if (type == "opaque")
            out = RenderType::Opaque;
        else if (type == "alpha_cut" || type == "cut" || type == "masked")
            out = RenderType::AlphaCut;
        else if (type == "alpha_blend" || type == "blend")
            out = RenderType::AlphaBlend;
        else if (type == "transmission")
            out = RenderType::Transmission;
        else if (type == "lines")
            out = RenderType::Lines;
        else
            return false;
        return true;
    }

    static int ResolveMeshIdx(Scene *s, const SceneNodeHandle &h, int slot)
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

                auto setVec4 = [ensureInstance](Scene *s, NodeId * /*nodeId*/, int meshIdx, const std::string &prop, vec4 value) {
                    if (meshIdx < 0) return;
                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (!mesh.material) return;
                    MaterialInstance *inst = ensureInstance(s, mesh);
                    if (!inst) return;
                    // Color/emissive live in the material buffer — no MarkNodeDirty
                    // (avoids UpdateNodeMatrices subtree walks on ATH flash paths).
                    bool changed = false;
                    if (prop == "base_color")
                        changed = inst->SetBaseColorFactor(value);
                    if (changed)
                        s->SetMaterialDirty();
                };
                auto setVec3 = [ensureInstance](Scene *s, NodeId * /*nodeId*/, int meshIdx, const std::string &prop, vec3 value) {
                    if (meshIdx < 0) return;
                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (!mesh.material) return;
                    MaterialInstance *inst = ensureInstance(s, mesh);
                    if (!inst) return;
                    bool changed = false;
                    if (prop == "base_color")
                        changed = inst->SetBaseColorFactor(vec4(value, 1.f));
                    else if (prop == "emissive")
                        changed = inst->SetEmissiveFactor(value);
                    if (changed)
                        s->SetMaterialDirty();
                };
                auto setFloat = [ensureInstance](Scene *s, NodeId * /*nodeId*/, int meshIdx, const std::string &prop, float value) {
                    if (meshIdx < 0) return;
                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (!mesh.material) return;
                    MaterialInstance *inst = ensureInstance(s, mesh);
                    if (!inst) return;
                    bool changed = false;
                    if (prop == "transmission")
                        changed = inst->SetTransmissionFactor(value);
                    else if (prop == "metallic")
                        changed = inst->SetMetallic(value);
                    else if (prop == "roughness")
                        changed = inst->SetRoughness(value);
                    else if (prop == "alpha_cutoff")
                        changed = inst->SetAlphaCutoff(value);
                    else if (prop == "occlusion_strength")
                        changed = inst->SetOcclusionStrength(value);
                    else if (prop == "normal_scale")
                        changed = inst->SetNormalScale(value);
                    else if (prop == "ior")
                        changed = inst->SetIor(value);
                    else if (prop == "thickness_factor")
                        changed = inst->SetThicknessFactor(value);
                    else if (prop == "attenuation_distance")
                        changed = inst->SetAttenuationDistance(value);
                    // No instance override: night emissive is a property of the material
                    // itself (a shared material is night-emissive for every user).
                    else if (prop == "night_emissive")
                    {
                        if (mesh.material->nightEmissive == value)
                            return;
                        mesh.material->nightEmissive = value;
                        changed = true;
                    }
                    else
                        return;
                    if (changed)
                        s->SetMaterialDirty();
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

                // Batched emissive writes (mesh slot 0): one crossing for N nodes.
                // `t` is a flat interleaved scratch array {node, r, g, b, ...}, `count` the
                // number of node entries; stale tail beyond count*4 is ignored. Invalid
                // handles are skipped. SetMaterialDirty fires once for the whole batch.
                mat.set_function("set_emissives", [ensureInstance](sol::table t, int count) {
                    Scene *s = GetScene();
                    if (!s) return;
                    bool anyChanged = false;
                    for (int i = 0; i < count; i++)
                    {
                        int base = i * 4;
                        auto h = t.raw_get<sol::optional<SceneNodeHandle>>(base + 1);
                        auto r = t.raw_get<sol::optional<float>>(base + 2);
                        auto g = t.raw_get<sol::optional<float>>(base + 3);
                        auto b = t.raw_get<sol::optional<float>>(base + 4);
                        if (!h || !r || !g || !b)
                            continue;
                        int meshIdx = ResolveMeshIdx(s, *h, 0);
                        if (meshIdx < 0)
                            continue;
                        Mesh &mesh = s->GetMesh(meshIdx);
                        if (!mesh.material)
                            continue;
                        MaterialInstance *inst = ensureInstance(s, mesh);
                        if (inst && inst->SetEmissiveFactor(vec3(*r, *g, *b)))
                            anyChanged = true;
                    }
                    if (anyChanged)
                        s->SetMaterialDirty();
                });

                auto getRenderTypeStr = [](Scene *s, int meshIdx) -> std::string {
                    if (meshIdx < 0) return "opaque";
                    switch (s->GetMeshes()[meshIdx].renderType)
                    {
                    case RenderType::Opaque: return "opaque";
                    case RenderType::AlphaCut: return "alpha_cut";
                    case RenderType::AlphaBlend: return "alpha_blend";
                    case RenderType::Transmission: return "transmission";
                    case RenderType::Lines: return "lines";
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

                auto setRenderTypeImpl = [](Scene *s, NodeId *nodeId, int meshIdx, const std::string &type) {
                    if (!s || !s->IsNodeAlive(nodeId) || meshIdx < 0) return;
                    RenderType rt;
                    if (!ParseRenderType(type, rt)) return;
                    SetNodeRenderType(s, nodeId, meshIdx, rt);
                };

                mat.set_function("set_render_type", sol::overload(
                    [setRenderTypeImpl](SceneNodeHandle &h, const std::string &type) {
                        Scene *s = GetScene();
                        setRenderTypeImpl(s, h.nodeId, ResolveMeshIdx(s, h), type);
                    },
                    [setRenderTypeImpl](SceneNodeHandle &h, int slot, const std::string &type) {
                        Scene *s = GetScene();
                        setRenderTypeImpl(s, h.nodeId, ResolveMeshIdx(s, h, slot), type);
                    }));

                auto getDoubleSided = [](Scene *s, int meshIdx) -> bool {
                    return GetNodeDoubleSided(s, meshIdx);
                };

                mat.set_function("is_double_sided", sol::overload(
                    [getDoubleSided](SceneNodeHandle &h) -> bool {
                        Scene *s = GetScene();
                        return getDoubleSided(s, ResolveMeshIdx(s, h));
                    },
                    [getDoubleSided](SceneNodeHandle &h, int slot) -> bool {
                        Scene *s = GetScene();
                        return getDoubleSided(s, ResolveMeshIdx(s, h, slot));
                    }));

                auto setDoubleSided = [](Scene *s, NodeId *nodeId, int meshIdx, bool doubleSided) {
                    SetNodeDoubleSided(s, nodeId, meshIdx, doubleSided);
                };

                mat.set_function("set_double_sided", sol::overload(
                    [setDoubleSided](SceneNodeHandle &h, bool doubleSided) {
                        Scene *s = GetScene();
                        setDoubleSided(s, h.nodeId, ResolveMeshIdx(s, h), doubleSided);
                    },
                    [setDoubleSided](SceneNodeHandle &h, int slot, bool doubleSided) {
                        Scene *s = GetScene();
                        setDoubleSided(s, h.nodeId, ResolveMeshIdx(s, h, slot), doubleSided);
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
                        return (mask & TextureBit(it->second)) != 0;
                    },
                    [getTexMask](SceneNodeHandle &h, int slot, const std::string &type) -> bool {
                        Scene *s = GetScene();
                        int meshIdx = ResolveMeshIdx(s, h, slot);
                        uint32_t mask = getTexMask(s, meshIdx);
                        auto it = s_texSlots.find(std::string_view(type));
                        if (it == s_texSlots.end()) return false;
                        return (mask & TextureBit(it->second)) != 0;
                    }));

                mat.set_function("set_texture", sol::overload(
                    [](SceneNodeHandle &h, const std::string &path) -> bool {
                        return SetNodeTexture(h, 0, "base_color", path);
                    },
                    [](SceneNodeHandle &h, const std::string &type, const std::string &path) -> bool {
                        return SetNodeTexture(h, 0, type, path);
                    },
                    [](SceneNodeHandle &h, int slot, const std::string &type, const std::string &path) -> bool {
                        return SetNodeTexture(h, slot, type, path);
                    }));

                mat.set_function("remove_texture", sol::overload(
                    [](SceneNodeHandle &h, const std::string &type) -> bool {
                        Scene *s = GetScene();
                        auto slotIt = s_texSlots.find(std::string_view(type));
                        return s && h.IsValid(*s) && slotIt != s_texSlots.end() &&
                               ClearTexture(s, h.nodeId, ResolveMeshIdx(s, h), slotIt->second);
                    },
                    [](SceneNodeHandle &h, int slot, const std::string &type) -> bool {
                        Scene *s = GetScene();
                        auto slotIt = s_texSlots.find(std::string_view(type));
                        return s && h.IsValid(*s) && slotIt != s_texSlots.end() &&
                               ClearTexture(s, h.nodeId, ResolveMeshIdx(s, h, slot), slotIt->second);
                    })); });
        }
    } s_materialBindings;
} // namespace pe
