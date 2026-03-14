#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Scene/Model.h"
#include "Scene/Primitives.h"
#include "Scene/Scene.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    static struct ModelBindings
    {
        ModelBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                       {
                lua.new_usertype<Model>("Model",
                    sol::no_constructor,
                    "get_id", &Model::GetId,
                    "get_label", &Model::GetLabel,
                    "set_label", &Model::SetLabel,
                    "get_file_path", [](Model &m) { return m.GetFilePath().string(); },
                    "is_primitive", &Model::IsPrimitive,
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
                    "set_scale", [](Model &m, const vec3 &scale) {
                        if (m.GetNodeInfos().empty()) return;
                        auto &mat = m.GetNodeInfos()[0].localMatrix;
                        vec3 pos(mat[3]);
                        mat = glm::translate(mat4(1.0f), pos);
                        mat = glm::scale(mat, scale);
                        m.MarkDirty(0);
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

                lua.set_function("load_model", [](const std::string &path) -> Model * {
                    std::string fullPath = path;
                    if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos)
                        fullPath = Path::Assets + "Models/" + path;

                    Model *model = Model::Load(fullPath);
                    if (!model) return nullptr;

                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().AddModel(model);
                    return model;
                });

                lua.set_function("remove_model", [](Model *model) {
                    if (!model) return;
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().RemoveModel(model);
                });

                sol::table prim = lua.create_named_table("primitives");
                prim.set_function("cube", [](sol::optional<float> size) -> Model * {
                    Model *m = Primitives::CreateCube(size.value_or(1.0f));
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r && m) r->GetScene().AddModel(m);
                    return m;
                });
                prim.set_function("sphere", [](sol::optional<float> radius) -> Model * {
                    Model *m = Primitives::CreateSphere(radius.value_or(1.0f));
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r && m) r->GetScene().AddModel(m);
                    return m;
                });
                prim.set_function("plane", [](sol::optional<float> width, sol::optional<float> depth) -> Model * {
                    Model *m = Primitives::CreatePlane(width.value_or(10.0f), depth.value_or(10.0f));
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r && m) r->GetScene().AddModel(m);
                    return m;
                });
                prim.set_function("cylinder", [](sol::optional<float> radius, sol::optional<float> height) -> Model * {
                    Model *m = Primitives::CreateCylinder(radius.value_or(1.0f), height.value_or(2.0f));
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r && m) r->GetScene().AddModel(m);
                    return m;
                });
                prim.set_function("cone", [](sol::optional<float> radius, sol::optional<float> height) -> Model * {
                    Model *m = Primitives::CreateCone(radius.value_or(1.0f), height.value_or(2.0f));
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r && m) r->GetScene().AddModel(m);
                    return m;
                }); });
        }
    } s_modelBindings;
} // namespace pe
#endif
