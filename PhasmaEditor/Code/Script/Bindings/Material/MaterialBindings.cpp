#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Scene/Model.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"
#include "API/RHI.h"
#include "API/Queue.h"
#include "API/Command.h"

namespace pe
{
    // materialFactors[0] layout (mat4):
    // Row 0: Base Color (RGBA)
    // Row 1: Emissive (RGB) + Transmission (A)
    // Row 2: Metallic (x), Roughness (y), AlphaCutoff (z), OcclusionStrength (w)
    // Row 3: Unused (x), NormalScale (y), Unused (z), Unused (w)

    static const std::unordered_map<std::string_view, uint32_t> s_texSlots = {
        {"base_color", 0}, {"metallic_roughness", 1}, {"normal", 2}, {"occlusion", 3}, {"emissive", 4}};

    static MeshInfo *GetMesh(Model &m, int index)
    {
        auto &meshes = m.GetMeshInfos();
        if (index < 0 || index >= static_cast<int>(meshes.size()))
            return nullptr;
        return &meshes[index];
    }

    static void MarkMeshDirty(Model &m, int meshIndex)
    {
        int nodeCount = m.GetNodeCount();
        for (int i = 0; i < nodeCount; i++)
        {
            if (m.GetNodeMesh(i) == meshIndex)
                m.MarkDirty(i);
        }
    }

    static struct MaterialBindings
    {
        MaterialBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table mat = lua.create_named_table("material");

                mat.set_function("get", [&lua](Model &model, int meshIndex) -> sol::object {
                    auto *mesh = GetMesh(model, meshIndex);
                    if (!mesh) return sol::nil;

                    sol::table t = lua.create_table();
                    t["base_color"] = vec4(mesh->materialFactors[0][0]);
                    t["emissive"] = vec3(mesh->materialFactors[0][1]);
                    t["transmission"] = mesh->materialFactors[0][1].w;
                    t["metallic"] = mesh->materialFactors[0][2].x;
                    t["roughness"] = mesh->materialFactors[0][2].y;
                    t["alpha_cutoff"] = mesh->materialFactors[0][2].z;
                    t["occlusion_strength"] = mesh->materialFactors[0][2].w;
                    t["normal_scale"] = mesh->materialFactors[0][3].y;
                    return t;
                });

                mat.set_function("set", sol::overload(
                    [](Model &model, int meshIndex, const std::string &prop, vec4 value) {
                        auto *mesh = GetMesh(model, meshIndex);
                        if (!mesh) return;
                        auto &f = mesh->materialFactors[0];
                        if (prop == "base_color") f[0] = value;
                        MarkMeshDirty(model, meshIndex);
                    },
                    [](Model &model, int meshIndex, const std::string &prop, vec3 value) {
                        auto *mesh = GetMesh(model, meshIndex);
                        if (!mesh) return;
                        auto &f = mesh->materialFactors[0];
                        if (prop == "base_color") { f[0].x = value.x; f[0].y = value.y; f[0].z = value.z; f[0].w = 1.0f; }
                        else if (prop == "emissive") { f[1].x = value.x; f[1].y = value.y; f[1].z = value.z; }
                        MarkMeshDirty(model, meshIndex);
                    },
                    [](Model &model, int meshIndex, const std::string &prop, float value) {
                        auto *mesh = GetMesh(model, meshIndex);
                        if (!mesh) return;
                        auto &f = mesh->materialFactors[0];
                        if (prop == "transmission") f[1].w = value;
                        else if (prop == "metallic") f[2].x = value;
                        else if (prop == "roughness") f[2].y = value;
                        else if (prop == "alpha_cutoff") f[2].z = value;
                        else if (prop == "occlusion_strength") f[2].w = value;
                        else if (prop == "normal_scale") f[3].y = value;
                        else return;
                        MarkMeshDirty(model, meshIndex);
                    }));

                mat.set_function("get_render_type", [](Model &model, int meshIndex) -> std::string {
                    auto *mesh = GetMesh(model, meshIndex);
                    if (!mesh) return "";
                    switch (mesh->renderType)
                    {
                    case RenderType::Opaque: return "opaque";
                    case RenderType::AlphaCut: return "alpha_cut";
                    case RenderType::AlphaBlend: return "alpha_blend";
                    case RenderType::Transmission: return "transmission";
                    default: return "";
                    }
                });

                mat.set_function("set_render_type", [](Model &model, int meshIndex, const std::string &type) {
                    auto *mesh = GetMesh(model, meshIndex);
                    if (!mesh) return;

                    RenderType rt;
                    if (type == "opaque") rt = RenderType::Opaque;
                    else if (type == "alpha_cut") rt = RenderType::AlphaCut;
                    else if (type == "alpha_blend") rt = RenderType::AlphaBlend;
                    else if (type == "transmission") rt = RenderType::Transmission;
                    else return;

                    if (mesh->renderType == rt) return;
                    mesh->renderType = rt;

                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r)
                    {
                        r->WaitAllFramesCommands();
                        r->GetScene().UpdateGeometryBuffers();
                    }
                });

                mat.set_function("get_texture_mask", [](Model &model, int meshIndex) -> uint32_t {
                    auto *mesh = GetMesh(model, meshIndex);
                    return mesh ? mesh->textureMask : 0;
                });

                mat.set_function("has_texture", [](Model &model, int meshIndex, const std::string &type) -> bool {
                    auto *mesh = GetMesh(model, meshIndex);
                    if (!mesh) return false;
                    auto it = s_texSlots.find(std::string_view(type));
                    if (it == s_texSlots.end()) return false;
                    return (mesh->textureMask & (1u << it->second)) != 0;
                });

                mat.set_function("set_texture", [](Model &model, int meshIndex, const std::string &type, const std::string &path) -> bool {
                    auto *mesh = GetMesh(model, meshIndex);
                    if (!mesh) return false;

                    auto it = s_texSlots.find(std::string_view(type));
                    if (it == s_texSlots.end())
                    {
                        PE_WARN("material.set_texture: unknown type '%s'", type.c_str());
                        return false;
                    }
                    uint32_t slot = it->second;

                    std::string fullPath = path;
                    if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos)
                        fullPath = Path::Assets + "Textures/" + path;

                    if (!std::filesystem::exists(fullPath))
                    {
                        PE_WARN("material.set_texture: file not found: %s", fullPath.c_str());
                        return false;
                    }

                    Queue *queue = RHII.GetMainQueue();
                    CommandBuffer *cmd = queue->AcquireCommandBuffer();
                    cmd->Begin();
                    ResourceHandle<Image> handle = model.LoadTexture(cmd, fullPath);
                    cmd->End();
                    queue->Submit(1, &cmd, nullptr, nullptr);
                    cmd->Wait();
                    cmd->Return();

                    if (!handle) return false;

                    mesh->images[slot] = handle;
                    mesh->textureMask |= (1u << slot);

                    if (!mesh->samplers[slot])
                        mesh->samplers[slot] = Model::GetDefaultResources().sampler;

                    MarkMeshDirty(model, meshIndex);
                    return true;
                });

                mat.set_function("remove_texture", [](Model &model, int meshIndex, const std::string &type) -> bool {
                    auto *mesh = GetMesh(model, meshIndex);
                    if (!mesh) return false;

                    auto it = s_texSlots.find(std::string_view(type));
                    if (it == s_texSlots.end()) return false;
                    uint32_t slot = it->second;

                    if (!(mesh->textureMask & (1u << slot))) return false;

                    mesh->images[slot] = {};
                    mesh->samplers[slot] = nullptr;
                    mesh->textureMask &= ~(1u << slot);
                    MarkMeshDirty(model, meshIndex);
                    return true;
                }); });
        }
    } s_materialBindings;
} // namespace pe
#endif
