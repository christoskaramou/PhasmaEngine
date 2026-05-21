#include "Script/ScriptSystem.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/Material.h"
#include "Scene/ModelAsset.h"
#include "Scene/Primitives.h"
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

    static constexpr float kSpriteEmissiveBoost = 4.0f;

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
            if (std::filesystem::exists(candidate))
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
        if (!std::filesystem::exists(resolvedPath))
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

    static ResourceHandle<Image> DefaultTextureForSlot(TextureType slot)
    {
        const auto &defaults = ModelAsset::GetDefaultResources();
        switch (slot)
        {
        case TextureType::Normal:
            return ResourceHandle<Image>::FromRaw(defaults.normal);
        case TextureType::Emissive:
            return ResourceHandle<Image>::FromRaw(defaults.black);
        default:
            return ResourceHandle<Image>::FromRaw(defaults.white);
        }
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

        inst->SetTexture(slot, DefaultTextureForSlot(slot));
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
        mesh.renderType = renderType;
        if (mesh.material)
        {
            MaterialInstance *inst = EnsureInstance(s, mesh);
            if (inst)
                inst->SetRenderType(renderType);
        }
        s->SetGeometryDirty();
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
        else
            return false;
        return true;
    }

    static bool ParseSpriteCoordinateSpace(std::string_view space, SpriteCoordinateSpace &out)
    {
        if (space == "world")
            out = SpriteCoordinateSpace::World;
        else if (space == "ndc" || space == "screen" || space == "normalized")
            out = SpriteCoordinateSpace::NDC;
        else
            return false;
        return true;
    }

    static void SetNodeTint(Scene *s, NodeId *nodeId, int meshIdx, const vec4 &tint)
    {
        if (!s || !s->IsNodeAlive(nodeId) || !s->IsValidMeshIndex(meshIdx))
            return;

        Mesh &mesh = s->GetMesh(meshIdx);
        if (!mesh.material)
            return;

        MaterialInstance *inst = EnsureInstance(s, mesh);
        if (!inst)
            return;

        inst->SetBaseColorFactor(tint);
        inst->SetEmissiveFactor(vec3(tint) * kSpriteEmissiveBoost);
        s->SetMaterialDirty();
        s->MarkNodeDirty(nodeId);
    }

    static std::vector<int> ReadFrameList(sol::table frames)
    {
        std::vector<int> result;
        for (auto &kv : frames)
        {
            sol::object value = kv.second;
            if (value.is<int>())
                result.push_back(value.as<int>());
        }
        return result;
    }

    static SceneNodeHandle CreateSpriteHandle(const std::string &texturePath,
                                              float width,
                                              float height,
                                              const vec3 &position,
                                              const vec4 &tint,
                                              RenderType renderType,
                                              const std::string &name)
    {
        Scene *s = GetScene();
        if (!s)
            return SceneNodeHandle();

        SceneNodeHandle handle = s->CreateSpriteNode(name.empty() ? "Sprite" : name,
                                                     vec2(width, height),
                                                     position,
                                                     renderType,
                                                     tint);
        if (!handle.IsValid(*s))
            return SceneNodeHandle();

        const int meshIdx = ResolveMeshIdx(s, handle);
        if (!texturePath.empty())
        {
            ResourceHandle<Image> image = LoadTextureForLua(texturePath);
            if (image)
                ApplyTexture(s, handle.nodeId, meshIdx, TextureType::BaseColor, image);
        }

        return handle;
    }

    static sol::object MakeSpriteObject(sol::this_state ts, const SceneNodeHandle &handle)
    {
        sol::state_view lua(ts);
        Scene *s = GetScene();
        if (!s || !handle.IsValid(*s))
            return sol::make_object(lua, sol::nil);
        return sol::make_object(lua, handle);
    }

    static void SetNodeSize2D(SceneNodeHandle &h, float width, float height)
    {
        Scene *s = GetScene();
        if (!s || !h.IsValid(*s))
            return;

        s->SetSpriteSize(h.nodeId, vec2(width, height));
    }

    static bool SetNodeNdc2D(SceneNodeHandle &h, const vec2 &position, const vec2 &size, float depth, float rotation)
    {
        Scene *s = GetScene();
        if (!s || !h.IsValid(*s))
            return false;

        s->SetSpriteNdcTransform(h.nodeId, position, size, depth, rotation);
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

                auto setVec4 = [ensureInstance](Scene *s, NodeId *nodeId, int meshIdx, const std::string &prop, vec4 value) {
                    if (meshIdx < 0) return;
                    Mesh &mesh = s->GetMesh(meshIdx);
                    if (!mesh.material) return;
                    MaterialInstance *inst = ensureInstance(s, mesh);
                    if (!inst) return;
                    if (prop == "base_color") inst->SetBaseColorFactor(value);
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
                    }));

                sol::table sprite = lua.create_named_table("sprite");
                sprite.set_function("create", sol::overload(
                    [](const std::string &texturePath, sol::optional<float> width, sol::optional<float> height, sol::this_state ts) -> sol::object {
                        SceneNodeHandle handle = CreateSpriteHandle(texturePath, width.value_or(1.0f), height.value_or(1.0f),
                                                                    vec3(0.0f), vec4(1.0f), RenderType::AlphaCut, "Sprite");
                        return MakeSpriteObject(ts, handle);
                    },
                    [](sol::table desc, sol::this_state ts) -> sol::object {
                        std::string texturePath;
                        if (desc["texture"].valid())
                            texturePath = desc["texture"].get<std::string>();
                        else if (desc["path"].valid())
                            texturePath = desc["path"].get<std::string>();

                        float width = desc["width"].valid() ? desc["width"].get<float>() : 1.0f;
                        float height = desc["height"].valid() ? desc["height"].get<float>() : 1.0f;

                        vec3 position(0.0f);
                        if (desc["position"].valid())
                            position = desc["position"].get<vec3>();
                        else
                        {
                            if (desc["x"].valid()) position.x = desc["x"].get<float>();
                            if (desc["y"].valid()) position.y = desc["y"].get<float>();
                            if (desc["z"].valid()) position.z = desc["z"].get<float>();
                        }

                        vec4 tint(1.0f);
                        if (desc["tint"].valid())
                            tint = desc["tint"].get<vec4>();
                        else if (desc["color"].valid())
                            tint = desc["color"].get<vec4>();

                        RenderType renderType = RenderType::AlphaCut;
                        if (desc["render_type"].valid())
                        {
                            RenderType parsed;
                            if (ParseRenderType(desc["render_type"].get<std::string>(), parsed))
                                renderType = parsed;
                        }
                        else if (desc["alpha_mode"].valid())
                        {
                            RenderType parsed;
                            if (ParseRenderType(desc["alpha_mode"].get<std::string>(), parsed))
                                renderType = parsed;
                        }

                        std::string name;
                        if (desc["name"].valid())
                            name = desc["name"].get<std::string>();

                        SceneNodeHandle handle = CreateSpriteHandle(texturePath, width, height, position, tint, renderType, name);
                        Scene *s = GetScene();
                        if (s && handle.IsValid(*s))
                        {
                            SpriteCoordinateSpace coordinateSpace = SpriteCoordinateSpace::World;
                            if (desc["space"].valid())
                                ParseSpriteCoordinateSpace(desc["space"].get<std::string>(), coordinateSpace);
                            else if (desc["coordinate_space"].valid())
                                ParseSpriteCoordinateSpace(desc["coordinate_space"].get<std::string>(), coordinateSpace);

                            if (coordinateSpace == SpriteCoordinateSpace::NDC)
                            {
                                vec2 ndcPosition(position.x, position.y);
                                if (desc["ndc_position"].valid())
                                    ndcPosition = desc["ndc_position"].get<vec2>();

                                vec2 ndcSize(width, height);
                                if (desc["ndc_size"].valid())
                                    ndcSize = desc["ndc_size"].get<vec2>();

                                float depth = desc["depth"].valid() ? desc["depth"].get<float>() : position.z;
                                if (depth <= 0.0f)
                                    depth = 1.0f;

                                float rotation = 0.0f;
                                if (desc["rotation"].valid())
                                    rotation = desc["rotation"].get<float>();
                                else if (desc["rotation_degrees"].valid())
                                    rotation = radians(desc["rotation_degrees"].get<float>());

                                s->SetSpriteNdcTransform(handle.nodeId, ndcPosition, ndcSize, depth, rotation);
                            }

                            if (desc["uv_rect"].valid())
                                s->SetSpriteUvRect(handle.nodeId, desc["uv_rect"].get<vec4>());
                            else if (desc["frame"].valid() || desc["columns"].valid() || desc["rows"].valid())
                            {
                                int columns = desc["columns"].valid() ? desc["columns"].get<int>() : 1;
                                int rows = desc["rows"].valid() ? desc["rows"].get<int>() : 1;
                                int frame = desc["frame"].valid() ? desc["frame"].get<int>() : 0;
                                s->SetSpriteFrame(handle.nodeId, frame, columns, rows);
                            }

                            if (desc["animation"].valid())
                            {
                                sol::table animation = desc["animation"].get<sol::table>();
                                int columns = animation["columns"].valid() ? animation["columns"].get<int>() : 1;
                                int rows = animation["rows"].valid() ? animation["rows"].get<int>() : 1;
                                float fps = animation["fps"].valid() ? animation["fps"].get<float>() : 12.0f;
                                bool loop = animation["loop"].valid() ? animation["loop"].get<bool>() : true;
                                std::vector<int> frames;
                                if (animation["frames"].valid())
                                    frames = ReadFrameList(animation["frames"].get<sol::table>());
                                s->SetSpriteAnimation(handle.nodeId, columns, rows, fps, frames, loop);
                            }
                        }

                        return MakeSpriteObject(ts, handle);
                    }));

                sprite.set_function("set_texture", [](SceneNodeHandle &h, const std::string &path) -> bool {
                    return SetNodeTexture(h, 0, "base_color", path);
                });
                sprite.set_function("set_tint", [](SceneNodeHandle &h, const vec4 &tint) {
                    Scene *s = GetScene();
                    if (s && h.IsValid(*s))
                        SetNodeTint(s, h.nodeId, ResolveMeshIdx(s, h), tint);
                });
                sprite.set_function("set_render_type", [](SceneNodeHandle &h, const std::string &type) {
                    Scene *s = GetScene();
                    RenderType renderType;
                    if (s && h.IsValid(*s) && ParseRenderType(type, renderType))
                        SetNodeRenderType(s, h.nodeId, ResolveMeshIdx(s, h), renderType);
                });
                sprite.set_function("set_size", [](SceneNodeHandle &h, float width, float height) {
                    SetNodeSize2D(h, width, height);
                });
                sprite.set_function("set_space", [](SceneNodeHandle &h, const std::string &space) -> bool {
                    Scene *s = GetScene();
                    SpriteCoordinateSpace parsed;
                    if (!s || !h.IsValid(*s) || !ParseSpriteCoordinateSpace(space, parsed))
                        return false;
                    s->SetSpriteCoordinateSpace(h.nodeId, parsed);
                    return true;
                });
                sprite.set_function("set_ndc", sol::overload(
                    [](SceneNodeHandle &h, const vec2 &position, const vec2 &size, sol::optional<float> depth, sol::optional<float> rotation) -> bool {
                        return SetNodeNdc2D(h, position, size, depth.value_or(1.0f), rotation.value_or(0.0f));
                    },
                    [](SceneNodeHandle &h, float x, float y, float width, float height, sol::optional<float> depth, sol::optional<float> rotation) -> bool {
                        return SetNodeNdc2D(h, vec2(x, y), vec2(width, height), depth.value_or(1.0f), rotation.value_or(0.0f));
                    },
                    [](SceneNodeHandle &h, sol::table desc) -> bool {
                        vec2 position(0.0f);
                        if (desc["position"].valid())
                            position = desc["position"].get<vec2>();
                        else
                        {
                            if (desc["x"].valid()) position.x = desc["x"].get<float>();
                            if (desc["y"].valid()) position.y = desc["y"].get<float>();
                        }

                        vec2 size(0.25f);
                        if (desc["size"].valid())
                            size = desc["size"].get<vec2>();
                        else
                        {
                            if (desc["width"].valid()) size.x = desc["width"].get<float>();
                            if (desc["height"].valid()) size.y = desc["height"].get<float>();
                        }

                        float depth = desc["depth"].valid() ? desc["depth"].get<float>() : 1.0f;
                        float rotation = 0.0f;
                        if (desc["rotation"].valid())
                            rotation = desc["rotation"].get<float>();
                        else if (desc["rotation_degrees"].valid())
                            rotation = radians(desc["rotation_degrees"].get<float>());

                        return SetNodeNdc2D(h, position, size, depth, rotation);
                    }));
                sprite.set_function("set_uv_rect", sol::overload(
                    [](SceneNodeHandle &h, const vec4 &uvRect) -> bool {
                        Scene *s = GetScene();
                        return s && h.IsValid(*s) && s->SetSpriteUvRect(h.nodeId, uvRect);
                    },
                    [](SceneNodeHandle &h, float u0, float v0, float u1, float v1) -> bool {
                        Scene *s = GetScene();
                        return s && h.IsValid(*s) && s->SetSpriteUvRect(h.nodeId, vec4(u0, v0, u1, v1));
                    }));
                sprite.set_function("set_frame", sol::overload(
                    [](SceneNodeHandle &h, int frame, int columns, int rows) -> bool {
                        Scene *s = GetScene();
                        return s && h.IsValid(*s) && s->SetSpriteFrame(h.nodeId, frame, columns, rows);
                    },
                    [](SceneNodeHandle &h, sol::table desc) -> bool {
                        Scene *s = GetScene();
                        if (!s || !h.IsValid(*s)) return false;
                        int frame = desc["frame"].valid() ? desc["frame"].get<int>() : 0;
                        int columns = desc["columns"].valid() ? desc["columns"].get<int>() : 1;
                        int rows = desc["rows"].valid() ? desc["rows"].get<int>() : 1;
                        return s->SetSpriteFrame(h.nodeId, frame, columns, rows);
                    }));
                sprite.set_function("play_animation", [](SceneNodeHandle &h, sol::table desc) -> bool {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return false;
                    int columns = desc["columns"].valid() ? desc["columns"].get<int>() : 1;
                    int rows = desc["rows"].valid() ? desc["rows"].get<int>() : 1;
                    float fps = desc["fps"].valid() ? desc["fps"].get<float>() : 12.0f;
                    bool loop = desc["loop"].valid() ? desc["loop"].get<bool>() : true;
                    std::vector<int> frames;
                    if (desc["frames"].valid())
                        frames = ReadFrameList(desc["frames"].get<sol::table>());
                    s->SetSpriteAnimation(h.nodeId, columns, rows, fps, frames, loop);
                    return true;
                });
                sprite.set_function("stop_animation", [](SceneNodeHandle &h) {
                    Scene *s = GetScene();
                    if (s && h.IsValid(*s))
                        s->StopSpriteAnimation(h.nodeId);
                });
                sprite.set_function("update_animation", [](SceneNodeHandle &h, float dt) -> bool {
                    Scene *s = GetScene();
                    return s && h.IsValid(*s) && s->UpdateSpriteAnimation(h.nodeId, dt);
                });
                sprite.set_function("update_all", [](float dt) {
                    Scene *s = GetScene();
                    if (s)
                        s->UpdateSpriteAnimations(dt);
                }); });
        }
    } s_materialBindings;
} // namespace pe
