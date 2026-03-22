#include "Script/ScriptSystem.h"
#include "Scene/ModelAsset.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"
#include "Camera/Camera.h"
#include "Base/ThreadPool.h"

namespace pe
{
    // Construct a std::filesystem::path from a UTF-8 std::string (avoids system code page conversion on Windows)
    static std::filesystem::path U8Path(const std::string &utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }

    // Add/remove models directly so Lua scripts see immediate results.
    // Safe: Lua runs on the main thread (init/update), same context as Window::ProcessEvents.
    static void AddModelToScene(ModelAsset *model)
    {
        if (!model)
            return;
        auto *r = GetGlobalSystem<RendererSystem>();
        if (!r)
            return;
        r->WaitAllFramesCommands();
        r->GetScene().AddModel(model);
        r->GetScene().UpdateGeometryBuffers();
        model->SetRenderReady(true);
    }

    static void RemoveModelFromScene(ModelAsset *model)
    {
        if (!model)
            return;
        auto *r = GetGlobalSystem<RendererSystem>();
        if (!r)
            return;
        r->WaitAllFramesCommands();
        r->GetScene().RemoveModel(model);
        r->GetScene().UpdateGeometryBuffers();
    }
    static ModelAsset *CloneSource(ModelAsset &source)
    {
        if (source.IsPrimitive())
        {
            const std::string &type = source.GetPrimitiveType();
            if (type == "cube")
                return Primitives::CreateCube();
            if (type == "sphere")
                return Primitives::CreateSphere();
            if (type == "plane")
                return Primitives::CreatePlane();
            if (type == "cylinder")
                return Primitives::CreateCylinder();
            if (type == "cone")
                return Primitives::CreateCone();
            if (type == "quad")
                return Primitives::CreateQuad();
            return nullptr;
        }
        std::string path = source.GetFilePath().string();
        if (path.empty())
            return nullptr;
        return ModelAsset::Load(path);
    }

    static AABB ComputeWorldBB(ModelAsset &m)
    {
        AABB bb;
        bool first = true;
        for (int i = 0; i < m.GetNodeCount(); i++)
        {
            const AABB &wbb = m.GetNodeWorldBoundingBox(i);
            if (first)
            {
                bb = wbb;
                first = false;
            }
            else
            {
                bb.min = glm::min(bb.min, wbb.min);
                bb.max = glm::max(bb.max, wbb.max);
            }
        }
        return bb;
    }

    static struct ModelBindings
    {
        ModelBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                lua.new_usertype<ModelAsset>("Model",
                    sol::no_constructor,
                    sol::meta_function::to_string, [](ModelAsset &m) { return m.GetLabel(); },
                    "get_id", &ModelAsset::GetId,
                    "get_label", &ModelAsset::GetLabel,
                    "set_label", &ModelAsset::SetLabel,
                    "get_file_path", [](ModelAsset &m) { return m.GetFilePath().string(); },
                    "is_primitive", &ModelAsset::IsPrimitive,
                    "is_visible", &ModelAsset::IsRenderReady,
                    "set_visible", &ModelAsset::SetRenderReady,
                    "get_primitive_type", &ModelAsset::GetPrimitiveType,
                    "get_node_count", &ModelAsset::GetNodeCount,
                    "get_mesh_count", &ModelAsset::GetMeshCount,
                    "get_vertex_count", &ModelAsset::GetVerticesCount,
                    "get_index_count", &ModelAsset::GetIndicesCount,
                    "get_position", [](ModelAsset &m) -> vec3 {
                        int root = m.GetRootNodeIndex();
                        if (root < 0) return vec3(0.f);
                        return vec3(m.GetNodeLocalMatrix(root)[3]);
                    },
                    "set_position", [](ModelAsset &m, const vec3 &pos) {
                        int root = m.GetRootNodeIndex();
                        if (root < 0) return;
                        mat4 mat = m.GetNodeLocalMatrix(root);
                        mat[3] = vec4(pos, 1.0f);
                        m.SetNodeLocalMatrix(root, mat);
                    },
                    "get_scale", [](ModelAsset &m) -> vec3 {
                        int root = m.GetRootNodeIndex();
                        if (root < 0) return vec3(1.f);
                        const mat4 &mat = m.GetNodeLocalMatrix(root);
                        return vec3(
                            glm::length(vec3(mat[0])),
                            glm::length(vec3(mat[1])),
                            glm::length(vec3(mat[2])));
                    },
                    "get_rotation", [](ModelAsset &m) -> vec3 {
                        int root = m.GetRootNodeIndex();
                        if (root < 0) return vec3(0.f);
                        const mat4 &mat = m.GetNodeLocalMatrix(root);
                        vec3 scale(
                            glm::length(vec3(mat[0])),
                            glm::length(vec3(mat[1])),
                            glm::length(vec3(mat[2])));
                        mat3 rotMat(
                            vec3(mat[0]) / scale.x,
                            vec3(mat[1]) / scale.y,
                            vec3(mat[2]) / scale.z);
                        return glm::degrees(glm::eulerAngles(glm::quat_cast(rotMat)));
                    },
                    "set_transform", [](ModelAsset &m, const vec3 &pos, const vec3 &rot_deg, const vec3 &scale) {
                        int root = m.GetRootNodeIndex();
                        if (root < 0) return;
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat(vec3(
                            glm::radians(rot_deg.x),
                            glm::radians(rot_deg.y),
                            glm::radians(rot_deg.z))));
                        mat4 S = glm::scale(mat4(1.0f), scale);
                        m.SetNodeLocalMatrix(root, T * R * S);
                    },
                    "set_rotation", [](ModelAsset &m, const vec3 &rot_deg) {
                        int root = m.GetRootNodeIndex();
                        if (root < 0) return;
                        const mat4 &mat = m.GetNodeLocalMatrix(root);
                        vec3 pos(mat[3]);
                        vec3 s(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat(vec3(glm::radians(rot_deg.x), glm::radians(rot_deg.y), glm::radians(rot_deg.z))));
                        mat4 S = glm::scale(mat4(1.0f), s);
                        m.SetNodeLocalMatrix(root, T * R * S);
                    },
                    "set_scale", [](ModelAsset &m, const vec3 &newScale) {
                        int root = m.GetRootNodeIndex();
                        if (root < 0) return;
                        const mat4 &mat = m.GetNodeLocalMatrix(root);
                        vec3 pos(mat[3]);
                        vec3 oldScale(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                        mat3 rotMat(vec3(mat[0]) / oldScale.x, vec3(mat[1]) / oldScale.y, vec3(mat[2]) / oldScale.z);
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat_cast(rotMat));
                        mat4 S = glm::scale(mat4(1.0f), newScale);
                        m.SetNodeLocalMatrix(root, T * R * S);
                    },
                    "get_bounding_box", [&lua](ModelAsset &m) -> sol::table {
                        AABB bb = ComputeWorldBB(m);
                        sol::table t = lua.create_table();
                        t["min"] = bb.min;
                        t["max"] = bb.max;
                        t["center"] = bb.GetCenter();
                        t["size"] = bb.GetSize();
                        return t;
                    },
                    "get_node_mesh", &ModelAsset::GetNodeMesh,
                    "get_node_name", [](ModelAsset &m, int node) -> std::string {
                        return m.GetNodeName(node);
                    },
                    "get_node_position", [](ModelAsset &m, int node) -> vec3 {
                        return vec3(m.GetNodeLocalMatrix(node)[3]);
                    },
                    "set_node_position", [](ModelAsset &m, int node, const vec3 &pos) {
                        mat4 mat = m.GetNodeLocalMatrix(node);
                        mat[3] = vec4(pos, 1.0f);
                        m.SetNodeLocalMatrix(node, mat);
                    },
                    "set_node_transform", [](ModelAsset &m, int node, const vec3 &pos, const vec3 &rot_deg, const vec3 &scale) {
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat(vec3(
                            glm::radians(rot_deg.x),
                            glm::radians(rot_deg.y),
                            glm::radians(rot_deg.z))));
                        mat4 S = glm::scale(mat4(1.0f), scale);
                        m.SetNodeLocalMatrix(node, T * R * S);
                    },
                    "get_node_world_position", [](ModelAsset &m, int node) -> vec3 {
                        return vec3(m.GetNodeWorldMatrix(node)[3]);
                    },
                    "get_node_parent", [](ModelAsset &m, int node) -> int {
                        return m.GetNodeParentIndex(node);
                    },
                    "get_node_children", [](ModelAsset &m, int node) -> sol::as_table_t<std::vector<int>> {
                        return sol::as_table(std::vector<int>(m.GetNodeChildren(node).begin(), m.GetNodeChildren(node).end()));
                    },
                    "get_node_rotation", [](ModelAsset &m, int node) -> vec3 {
                        const mat4 &mat = m.GetNodeLocalMatrix(node);
                        vec3 scale(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                        mat3 rotMat(vec3(mat[0]) / scale.x, vec3(mat[1]) / scale.y, vec3(mat[2]) / scale.z);
                        return glm::degrees(glm::eulerAngles(glm::quat_cast(rotMat)));
                    },
                    "get_node_scale", [](ModelAsset &m, int node) -> vec3 {
                        const mat4 &mat = m.GetNodeLocalMatrix(node);
                        return vec3(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                    },
                    "set_node_rotation", [](ModelAsset &m, int node, const vec3 &rot_deg) {
                        const mat4 &mat = m.GetNodeLocalMatrix(node);
                        vec3 pos(mat[3]);
                        vec3 s(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat(vec3(glm::radians(rot_deg.x), glm::radians(rot_deg.y), glm::radians(rot_deg.z))));
                        mat4 S = glm::scale(mat4(1.0f), s);
                        m.SetNodeLocalMatrix(node, T * R * S);
                    },
                    "set_node_scale", [](ModelAsset &m, int node, const vec3 &newScale) {
                        const mat4 &mat = m.GetNodeLocalMatrix(node);
                        vec3 pos(mat[3]);
                        vec3 oldScale(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                        mat3 rotMat(vec3(mat[0]) / oldScale.x, vec3(mat[1]) / oldScale.y, vec3(mat[2]) / oldScale.z);
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat_cast(rotMat));
                        mat4 S = glm::scale(mat4(1.0f), newScale);
                        m.SetNodeLocalMatrix(node, T * R * S);
                    },
                    "get_node_world_matrix", [](ModelAsset &m, int node) -> mat4 {
                        return m.GetNodeWorldMatrix(node);
                    },
                    "get_node_local_matrix", [](ModelAsset &m, int node) -> mat4 {
                        return m.GetNodeLocalMatrix(node);
                    },
                    "get_node_bounding_box", [&lua](ModelAsset &m, int node) -> sol::table {
                        sol::table t = lua.create_table();
                        const AABB &bb = m.GetNodeWorldBoundingBox(node);
                        t["min"] = bb.min;
                        t["max"] = bb.max;
                        t["center"] = bb.GetCenter();
                        t["size"] = bb.GetSize();
                        return t;
                    },
                    "reparent_node", [](ModelAsset &m, int node, int newParent) {
                        m.ReparentNode(node, newParent);
                    },
                    "get_mesh_info", [&lua](ModelAsset &m, int meshIdx) -> sol::table {
                        sol::table t = lua.create_table();
                        auto &meshes = m.GetMeshInfos();
                        if (meshIdx < 0 || meshIdx >= static_cast<int>(meshes.size())) return t;
                        auto &mi = meshes[meshIdx];
                        t["vertex_count"] = mi.verticesCount;
                        t["index_count"] = mi.indicesCount;
                        auto &bb = mi.boundingBox;
                        sol::table bbt = lua.create_table();
                        bbt["min"] = bb.min;
                        bbt["max"] = bb.max;
                        bbt["center"] = bb.GetCenter();
                        bbt["size"] = bb.GetSize();
                        t["bounding_box"] = bbt;
                        const char *rtStr = "opaque";
                        switch (mi.renderType)
                        {
                        case RenderType::AlphaCut: rtStr = "alpha_cut"; break;
                        case RenderType::AlphaBlend: rtStr = "alpha_blend"; break;
                        case RenderType::Transmission: rtStr = "transmission"; break;
                        default: break;
                        }
                        t["render_type"] = rtStr;
                        t["texture_mask"] = mi.textureMask;
                        return t;
                    });

                lua.set_function("get_models", []() -> sol::as_table_t<std::vector<ModelAsset *>> {
                    std::vector<ModelAsset *> result;
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r)
                    {
                        for (auto *m : r->GetScene().GetModels())
                            result.push_back(m);
                    }
                    return sol::as_table(std::move(result));
                });

                lua.set_function("find_model", [](const std::string &query) -> ModelAsset * {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return nullptr;

                    std::string q = query;
                    for (auto &c : q) c = static_cast<char>(std::tolower(c));

                    for (auto *m : r->GetScene().GetModels())
                    {
                        std::string name = m->GetLabel().empty()
                            ? m->GetFilePath().filename().string()
                            : m->GetLabel();
                        for (auto &c : name) c = static_cast<char>(std::tolower(c));

                        if (name.find(q) != std::string::npos)
                            return m;
                    }
                    return nullptr;
                });

                lua.set_function("load_model", [](const std::string &path) -> std::tuple<ModelAsset *, std::string> {
                    std::string fullPath = path;
                    if (!U8Path(path).is_absolute())
                        fullPath = Path::Assets + "Objects/" + path;

                    if (!std::filesystem::exists(U8Path(fullPath)))
                    {
                        std::string err = "load_model: file not found '" + fullPath + "'";
                        Log::Error(err);
                        return {nullptr, err};
                    }

                    ModelAsset *model = ModelAsset::Load(U8Path(fullPath));
                    if (!model)
                    {
                        std::string err = "load_model: could not parse '" + fullPath + "'";
                        Log::Error(err);
                        return {nullptr, err};
                    }
                    AddModelToScene(model);
                    return {model, std::string{}};
                });

                // load_model_async(path, callback) - full load on background thread,
                // scene integration + callback on main thread when ready
                lua.set_function("load_model_async", [](const std::string &path, sol::function callback) {
                    std::string fullPath = path;
                    if (!U8Path(path).is_absolute())
                        fullPath = Path::Assets + "Objects/" + path;

                    auto future = ThreadPool::General.Enqueue([fullPath]() -> ModelAsset * {
                        return ModelAsset::Load(U8Path(fullPath));
                    });

                    auto *ss = GetGlobalSystem<ScriptSystem>();
                    if (ss)
                    {
                        PendingAsyncLoad load;
                        load.future = std::move(future);
                        load.callback = std::move(callback);
                        ss->AddPendingAsyncLoad(std::move(load));
                    }
                });

                // load_models(paths, [callback]) - load multiple models in parallel on background threads,
                // scene integration on main thread as each completes, optional callback(models_table) when all done
                lua.set_function("load_models", [&lua](sol::table paths, sol::optional<sol::function> callback) {
                    auto *ss = GetGlobalSystem<ScriptSystem>();
                    if (!ss) return;

                    int count = 0;
                    for (auto &kv : paths)
                    {
                        if (kv.second.is<std::string>())
                            count++;
                    }
                    if (count == 0) return;

                    // Shared state for tracking completion across async loads
                    auto state = std::make_shared<BatchLoadState>();
                    state->total = count;

                    for (auto &kv : paths)
                    {
                        if (!kv.second.is<std::string>()) continue;

                        std::string path = kv.second.as<std::string>();
                        std::string fullPath = path;
                        if (!std::filesystem::path(path).is_absolute())
                            fullPath = Path::Assets + "Objects/" + path;

                        auto future = ThreadPool::General.Enqueue([fullPath]() -> ModelAsset * {
                            return ModelAsset::Load(fullPath);
                        });

                        PendingAsyncLoad load;
                        load.future = std::move(future);
                        load.batchState = state;
                        if (callback.has_value())
                            load.batchCallback = callback.value();
                        ss->AddPendingAsyncLoad(std::move(load));
                    }
                });

                lua.set_function("remove_model", [](ModelAsset *model) {
                    if (!model) return;
                    RemoveModelFromScene(model);
                });

                lua.set_function("clone_model", [](ModelAsset &source, sol::optional<float> x, sol::optional<float> y, sol::optional<float> z) -> ModelAsset * {
                    ModelAsset *m = CloneSource(source);
                    if (!m) return nullptr;

                    int root = m->GetRootNodeIndex();
                    if (root >= 0)
                    {
                        mat4 T = glm::translate(mat4(1.0f), vec3(x.value_or(0.f), y.value_or(0.f), z.value_or(0.f)));
                        m->SetNodeLocalMatrix(root, T);
                    }
                    AddModelToScene(m);
                    return m;
                });

                lua.set_function("scatter_models", [](ModelAsset &source, int count, float radius,
                                                      sol::optional<float> cx, sol::optional<float> cy, sol::optional<float> cz,
                                                      sol::optional<float> minScale, sol::optional<float> maxScale) -> sol::as_table_t<std::vector<ModelAsset *>> {
                    std::vector<ModelAsset *> result;
                    if (count <= 0) return sol::as_table(std::move(result));

                    float centerX = cx.value_or(0.f);
                    float centerY = cy.value_or(0.f);
                    float centerZ = cz.value_or(0.f);
                    float sMin = minScale.value_or(0.8f);
                    float sMax = maxScale.value_or(1.2f);

                    for (int i = 0; i < count; i++)
                    {
                        ModelAsset *m = CloneSource(source);
                        if (!m) continue;

                        float angle = static_cast<float>(std::rand()) / RAND_MAX * glm::two_pi<float>();
                        float dist = static_cast<float>(std::rand()) / RAND_MAX * radius;
                        float x = centerX + cos(angle) * dist;
                        float z = centerZ + sin(angle) * dist;

                        float s = sMin + static_cast<float>(std::rand()) / RAND_MAX * (sMax - sMin);
                        float rotY = static_cast<float>(std::rand()) / RAND_MAX * 360.0f;

                        mat4 T = glm::translate(mat4(1.0f), vec3(x, centerY, z));
                        mat4 R = glm::rotate(mat4(1.0f), glm::radians(rotY), vec3(0, 1, 0));
                        mat4 S = glm::scale(mat4(1.0f), vec3(s));

                        int root = m->GetRootNodeIndex();
                        if (root >= 0)
                            m->SetNodeLocalMatrix(root, T * R * S);
                        AddModelToScene(m);
                        result.push_back(m);
                    }
                    return sol::as_table(std::move(result));
                });

                lua.set_function("focus_camera_on", [](ModelAsset &model, sol::optional<float> distMult) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;
                    auto *cam = r->GetScene().GetActiveCamera();
                    if (!cam) return;

                    AABB bb = ComputeWorldBB(model);
                    vec3 center = bb.GetCenter();
                    float radius = glm::length(bb.GetSize()) * 0.5f;
                    float mult = distMult.value_or(2.0f);
                    vec3 dir = glm::normalize(cam->GetFront());
                    cam->SetPosition(center - dir * (radius * mult));
                });

                sol::table prim = lua.create_named_table("primitives");
                prim.set_function("cube", [](sol::optional<float> size) -> ModelAsset * {
                    ModelAsset *m = Primitives::CreateCube(size.value_or(1.0f));
                    AddModelToScene(m);
                    return m;
                });
                prim.set_function("sphere", [](sol::optional<float> radius) -> ModelAsset * {
                    ModelAsset *m = Primitives::CreateSphere(radius.value_or(1.0f));
                    AddModelToScene(m);
                    return m;
                });
                prim.set_function("plane", [](sol::optional<float> width, sol::optional<float> depth) -> ModelAsset * {
                    ModelAsset *m = Primitives::CreatePlane(width.value_or(10.0f), depth.value_or(10.0f));
                    AddModelToScene(m);
                    return m;
                });
                prim.set_function("cylinder", [](sol::optional<float> radius, sol::optional<float> height) -> ModelAsset * {
                    ModelAsset *m = Primitives::CreateCylinder(radius.value_or(1.0f), height.value_or(2.0f));
                    AddModelToScene(m);
                    return m;
                });
                prim.set_function("cone", [](sol::optional<float> radius, sol::optional<float> height) -> ModelAsset * {
                    ModelAsset *m = Primitives::CreateCone(radius.value_or(1.0f), height.value_or(2.0f));
                    AddModelToScene(m);
                    return m;
                });
                prim.set_function("quad", [](sol::optional<float> width, sol::optional<float> height) -> ModelAsset * {
                    ModelAsset *m = Primitives::CreateQuad(width.value_or(1.0f), height.value_or(1.0f));
                    AddModelToScene(m);
                    return m;
                }); });
        }
    } s_modelBindings;
} // namespace pe
