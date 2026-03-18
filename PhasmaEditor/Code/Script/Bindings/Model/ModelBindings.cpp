#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Scene/Model.h"
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
    static void AddModelToScene(Model *model)
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

    static void RemoveModelFromScene(Model *model)
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
    static Model *CloneSource(Model &source)
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
        return Model::Load(path);
    }

    static AABB ComputeWorldBB(Model &m)
    {
        AABB bb;
        bool first = true;
        for (auto &n : m.GetNodeInfos())
        {
            if (first)
            {
                bb = n.worldBoundingBox;
                first = false;
            }
            else
            {
                bb.min = glm::min(bb.min, n.worldBoundingBox.min);
                bb.max = glm::max(bb.max, n.worldBoundingBox.max);
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
                lua.new_usertype<Model>("Model",
                    sol::no_constructor,
                    sol::meta_function::to_string, [](Model &m) { return m.GetLabel(); },
                    "get_id", &Model::GetId,
                    "get_label", &Model::GetLabel,
                    "set_label", &Model::SetLabel,
                    "get_file_path", [](Model &m) { return m.GetFilePath().string(); },
                    "is_primitive", &Model::IsPrimitive,
                    "is_visible", &Model::IsRenderReady,
                    "set_visible", &Model::SetRenderReady,
                    "get_primitive_type", &Model::GetPrimitiveType,
                    "get_node_count", &Model::GetNodeCount,
                    "get_mesh_count", &Model::GetMeshCount,
                    "get_vertex_count", &Model::GetVerticesCount,
                    "get_index_count", &Model::GetIndicesCount,
                    "get_position", [](Model &m) -> vec3 {
                        if (m.GetNodeInfos().empty()) return vec3(0.f);
                        return vec3(m.GetNodeInfos()[0].localMatrix[3]);
                    },
                    "set_position", [](Model &m, const vec3 &pos) {
                        if (m.GetNodeInfos().empty()) return;
                        auto &mat = m.GetNodeInfos()[0].localMatrix;
                        mat[3] = vec4(pos, 1.0f);
                        m.MarkDirty(0);
                    },
                    "get_scale", [](Model &m) -> vec3 {
                        if (m.GetNodeInfos().empty()) return vec3(1.f);
                        auto &mat = m.GetNodeInfos()[0].localMatrix;
                        return vec3(
                            glm::length(vec3(mat[0])),
                            glm::length(vec3(mat[1])),
                            glm::length(vec3(mat[2])));
                    },
                    "get_rotation", [](Model &m) -> vec3 {
                        if (m.GetNodeInfos().empty()) return vec3(0.f);
                        auto &mat = m.GetNodeInfos()[0].localMatrix;
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
                    "set_transform", [](Model &m, const vec3 &pos, const vec3 &rot_deg, const vec3 &scale) {
                        if (m.GetNodeInfos().empty()) return;
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat(vec3(
                            glm::radians(rot_deg.x),
                            glm::radians(rot_deg.y),
                            glm::radians(rot_deg.z))));
                        mat4 S = glm::scale(mat4(1.0f), scale);
                        m.GetNodeInfos()[0].localMatrix = T * R * S;
                        m.MarkDirty(0);
                    },
                    "set_rotation", [](Model &m, const vec3 &rot_deg) {
                        if (m.GetNodeInfos().empty()) return;
                        auto &mat = m.GetNodeInfos()[0].localMatrix;
                        vec3 pos(mat[3]);
                        vec3 s(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat(vec3(glm::radians(rot_deg.x), glm::radians(rot_deg.y), glm::radians(rot_deg.z))));
                        mat4 S = glm::scale(mat4(1.0f), s);
                        mat = T * R * S;
                        m.MarkDirty(0);
                    },
                    "set_scale", [](Model &m, const vec3 &newScale) {
                        if (m.GetNodeInfos().empty()) return;
                        auto &mat = m.GetNodeInfos()[0].localMatrix;
                        vec3 pos(mat[3]);
                        vec3 oldScale(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                        mat3 rotMat(vec3(mat[0]) / oldScale.x, vec3(mat[1]) / oldScale.y, vec3(mat[2]) / oldScale.z);
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat_cast(rotMat));
                        mat4 S = glm::scale(mat4(1.0f), newScale);
                        mat = T * R * S;
                        m.MarkDirty(0);
                    },
                    "get_bounding_box", [&lua](Model &m) -> sol::table {
                        AABB bb = ComputeWorldBB(m);
                        sol::table t = lua.create_table();
                        t["min"] = bb.min;
                        t["max"] = bb.max;
                        t["center"] = bb.GetCenter();
                        t["size"] = bb.GetSize();
                        return t;
                    },
                    "get_node_mesh", &Model::GetNodeMesh,
                    "get_node_name", [](Model &m, int node) -> std::string {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return "";
                        return nodes[node].name;
                    },
                    "get_node_position", [](Model &m, int node) -> vec3 {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return vec3(0.f);
                        return vec3(nodes[node].localMatrix[3]);
                    },
                    "set_node_position", [](Model &m, int node, const vec3 &pos) {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return;
                        nodes[node].localMatrix[3] = vec4(pos, 1.0f);
                        m.MarkDirty(node);
                    },
                    "set_node_transform", [](Model &m, int node, const vec3 &pos, const vec3 &rot_deg, const vec3 &scale) {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return;
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat(vec3(
                            glm::radians(rot_deg.x),
                            glm::radians(rot_deg.y),
                            glm::radians(rot_deg.z))));
                        mat4 S = glm::scale(mat4(1.0f), scale);
                        nodes[node].localMatrix = T * R * S;
                        m.MarkDirty(node);
                    },
                    "get_node_world_position", [](Model &m, int node) -> vec3 {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return vec3(0.f);
                        return vec3(nodes[node].ubo.worldMatrix[3]);
                    },
                    "get_node_parent", [](Model &m, int node) -> int {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return -1;
                        return nodes[node].parent;
                    },
                    "get_node_children", [](Model &m, int node) -> sol::as_table_t<std::vector<int>> {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return sol::as_table(std::vector<int>{});
                        return sol::as_table(std::vector<int>(nodes[node].children.begin(), nodes[node].children.end()));
                    },
                    "get_node_rotation", [](Model &m, int node) -> vec3 {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return vec3(0.f);
                        auto &mat = nodes[node].localMatrix;
                        vec3 scale(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                        mat3 rotMat(vec3(mat[0]) / scale.x, vec3(mat[1]) / scale.y, vec3(mat[2]) / scale.z);
                        return glm::degrees(glm::eulerAngles(glm::quat_cast(rotMat)));
                    },
                    "get_node_scale", [](Model &m, int node) -> vec3 {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return vec3(1.f);
                        auto &mat = nodes[node].localMatrix;
                        return vec3(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                    },
                    "set_node_rotation", [](Model &m, int node, const vec3 &rot_deg) {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return;
                        auto &mat = nodes[node].localMatrix;
                        vec3 pos(mat[3]);
                        vec3 s(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat(vec3(glm::radians(rot_deg.x), glm::radians(rot_deg.y), glm::radians(rot_deg.z))));
                        mat4 S = glm::scale(mat4(1.0f), s);
                        mat = T * R * S;
                        m.MarkDirty(node);
                    },
                    "set_node_scale", [](Model &m, int node, const vec3 &newScale) {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return;
                        auto &mat = nodes[node].localMatrix;
                        vec3 pos(mat[3]);
                        vec3 oldScale(glm::length(vec3(mat[0])), glm::length(vec3(mat[1])), glm::length(vec3(mat[2])));
                        mat3 rotMat(vec3(mat[0]) / oldScale.x, vec3(mat[1]) / oldScale.y, vec3(mat[2]) / oldScale.z);
                        mat4 T = glm::translate(mat4(1.0f), pos);
                        mat4 R = glm::mat4_cast(glm::quat_cast(rotMat));
                        mat4 S = glm::scale(mat4(1.0f), newScale);
                        mat = T * R * S;
                        m.MarkDirty(node);
                    },
                    "get_node_world_matrix", [](Model &m, int node) -> mat4 {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return mat4(1.f);
                        return nodes[node].ubo.worldMatrix;
                    },
                    "get_node_local_matrix", [](Model &m, int node) -> mat4 {
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return mat4(1.f);
                        return nodes[node].localMatrix;
                    },
                    "get_node_bounding_box", [&lua](Model &m, int node) -> sol::table {
                        sol::table t = lua.create_table();
                        auto &nodes = m.GetNodeInfos();
                        if (node < 0 || node >= static_cast<int>(nodes.size())) return t;
                        auto &bb = nodes[node].worldBoundingBox;
                        t["min"] = bb.min;
                        t["max"] = bb.max;
                        t["center"] = bb.GetCenter();
                        t["size"] = bb.GetSize();
                        return t;
                    },
                    "reparent_node", [](Model &m, int node, int newParent) {
                        m.ReparentNode(node, newParent);
                    },
                    "get_mesh_info", [&lua](Model &m, int meshIdx) -> sol::table {
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

                lua.set_function("get_models", []() -> sol::as_table_t<std::vector<Model *>> {
                    std::vector<Model *> result;
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r)
                    {
                        for (auto *m : r->GetScene().GetModels())
                            result.push_back(m);
                    }
                    return sol::as_table(std::move(result));
                });

                lua.set_function("find_model", [](const std::string &query) -> Model * {
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

                lua.set_function("load_model", [](const std::string &path) -> std::tuple<Model *, std::string> {
                    std::string fullPath = path;
                    if (!U8Path(path).is_absolute())
                        fullPath = Path::Assets + "Objects/" + path;

                    if (!std::filesystem::exists(U8Path(fullPath)))
                    {
                        std::string err = "load_model: file not found '" + fullPath + "'";
                        Log::Error(err);
                        return {nullptr, err};
                    }

                    Model *model = Model::Load(U8Path(fullPath));
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

                    auto future = ThreadPool::General.Enqueue([fullPath]() -> Model * {
                        return Model::Load(U8Path(fullPath));
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

                        auto future = ThreadPool::General.Enqueue([fullPath]() -> Model * {
                            return Model::Load(fullPath);
                        });

                        PendingAsyncLoad load;
                        load.future = std::move(future);
                        load.batchState = state;
                        if (callback.has_value())
                            load.batchCallback = callback.value();
                        ss->AddPendingAsyncLoad(std::move(load));
                    }
                });

                lua.set_function("remove_model", [](Model *model) {
                    if (!model) return;
                    RemoveModelFromScene(model);
                });

                lua.set_function("clone_model", [](Model &source, sol::optional<float> x, sol::optional<float> y, sol::optional<float> z) -> Model * {
                    Model *m = CloneSource(source);
                    if (!m) return nullptr;

                    if (!m->GetNodeInfos().empty())
                    {
                        mat4 T = glm::translate(mat4(1.0f), vec3(x.value_or(0.f), y.value_or(0.f), z.value_or(0.f)));
                        m->GetNodeInfos()[0].localMatrix = T;
                        m->MarkDirty(0);
                    }
                    AddModelToScene(m);
                    return m;
                });

                lua.set_function("scatter_models", [](Model &source, int count, float radius,
                                                      sol::optional<float> cx, sol::optional<float> cy, sol::optional<float> cz,
                                                      sol::optional<float> minScale, sol::optional<float> maxScale) -> sol::as_table_t<std::vector<Model *>> {
                    std::vector<Model *> result;
                    if (count <= 0) return sol::as_table(std::move(result));

                    float centerX = cx.value_or(0.f);
                    float centerY = cy.value_or(0.f);
                    float centerZ = cz.value_or(0.f);
                    float sMin = minScale.value_or(0.8f);
                    float sMax = maxScale.value_or(1.2f);

                    for (int i = 0; i < count; i++)
                    {
                        Model *m = CloneSource(source);
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

                        if (!m->GetNodeInfos().empty())
                        {
                            m->GetNodeInfos()[0].localMatrix = T * R * S;
                            m->MarkDirty(0);
                        }
                        AddModelToScene(m);
                        result.push_back(m);
                    }
                    return sol::as_table(std::move(result));
                });

                lua.set_function("focus_camera_on", [](Model &model, sol::optional<float> distMult) {
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
                prim.set_function("cube", [](sol::optional<float> size) -> Model * {
                    Model *m = Primitives::CreateCube(size.value_or(1.0f));
                    AddModelToScene(m);
                    return m;
                });
                prim.set_function("sphere", [](sol::optional<float> radius) -> Model * {
                    Model *m = Primitives::CreateSphere(radius.value_or(1.0f));
                    AddModelToScene(m);
                    return m;
                });
                prim.set_function("plane", [](sol::optional<float> width, sol::optional<float> depth) -> Model * {
                    Model *m = Primitives::CreatePlane(width.value_or(10.0f), depth.value_or(10.0f));
                    AddModelToScene(m);
                    return m;
                });
                prim.set_function("cylinder", [](sol::optional<float> radius, sol::optional<float> height) -> Model * {
                    Model *m = Primitives::CreateCylinder(radius.value_or(1.0f), height.value_or(2.0f));
                    AddModelToScene(m);
                    return m;
                });
                prim.set_function("cone", [](sol::optional<float> radius, sol::optional<float> height) -> Model * {
                    Model *m = Primitives::CreateCone(radius.value_or(1.0f), height.value_or(2.0f));
                    AddModelToScene(m);
                    return m;
                });
                prim.set_function("quad", [](sol::optional<float> width, sol::optional<float> height) -> Model * {
                    Model *m = Primitives::CreateQuad(width.value_or(1.0f), height.value_or(1.0f));
                    AddModelToScene(m);
                    return m;
                }); });
        }
    } s_modelBindings;
} // namespace pe
#endif
