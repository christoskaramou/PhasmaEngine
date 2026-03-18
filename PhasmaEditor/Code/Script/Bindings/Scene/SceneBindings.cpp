#if defined(PE_SCRIPTS)
#include "Script/ScriptSystem.h"
#include "Scene/Scene.h"
#include "Scene/Model.h"
#include "Scene/SelectionManager.h"
#include "Camera/Camera.h"
#include "Systems/RendererSystem.h"
#include "Systems/LightSystem.h"

namespace pe
{
    static struct SceneBindings
    {
        SceneBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table scene = lua.create_named_table("scene");

                scene.set_function("save", [](const std::string &name) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().SaveScene(Path::Assets + "Scenes/" + name);
                });

                scene.set_function("load", [](const std::string &name) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().LoadScene(Path::Assets + "Scenes/" + name);
                });

                scene.set_function("clear", []() {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r)
                    {
                        std::vector<Model *> models;
                        for (auto *m : r->GetScene().GetModels())
                            models.push_back(m);
                        if (!models.empty())
                            EventSystem::PushEvent(EventType::ModelsRemoved, std::move(models));
                    }

                    auto *ls = GetGlobalSystem<LightSystem>();
                    if (ls)
                    {
                        ls->GetPointLights().clear();
                        ls->GetDirectionalLights().clear();
                        ls->GetSpotLights().clear();
                        ls->GetAreaLights().clear();
                    }

                    SelectionManager::Instance().ClearSelection();
                });

                scene.set_function("get_entities", [&lua]() -> sol::as_table_t<std::vector<sol::table>> {
                    std::vector<sol::table> result;
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r)
                    {
                        for (auto *m : r->GetScene().GetModels())
                        {
                            sol::table t = lua.create_table();
                            t["type"] = "model";
                            t["model"] = sol::make_object(lua, m);
                            t["label"] = m->GetLabel();
                            t["is_primitive"] = m->IsPrimitive();
                            result.push_back(t);
                        }
                    }
                    return sol::as_table(std::move(result));
                });

                scene.set_function("get_models", []() -> sol::as_table_t<std::vector<Model *>> {
                    std::vector<Model *> result;
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r)
                    {
                        for (auto *m : r->GetScene().GetModels())
                            result.push_back(m);
                    }
                    return sol::as_table(std::move(result));
                });

                scene.set_function("get_model_count", []() -> int {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    return r ? static_cast<int>(r->GetScene().GetModels().size()) : 0;
                });

                // find_model(label) -> Model* or nil — check before load_model to avoid duplicate loads and redundant events
                scene.set_function("find_model", [](const std::string &label) -> Model * {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return nullptr;
                    for (auto *m : r->GetScene().GetModels())
                        if (m->GetLabel() == label)
                            return m;
                    return nullptr;
                });

                scene.set_function("get_cameras", []() -> sol::as_table_t<std::vector<Camera *>> {
                    std::vector<Camera *> result;
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r)
                    {
                        for (auto *c : r->GetScene().GetCameras())
                            result.push_back(c);
                    }
                    return sol::as_table(std::move(result));
                });

                scene.set_function("get_active_camera", []() -> Camera * {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    return r ? r->GetScene().GetActiveCamera() : nullptr;
                });

                scene.set_function("add_camera", []() -> Camera * {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    return r ? r->GetScene().AddCamera() : nullptr;
                });

                scene.set_function("remove_camera", [](Camera *camera) {
                    if (!camera) return;
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().RemoveCamera(camera);
                });

                scene.set_function("set_active_camera", [](Camera *camera) {
                    if (!camera) return;
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().SetActiveCamera(camera);
                });

                // Selection
                sol::table selection = lua.create_named_table("selection");

                selection.set_function("get", [&lua]() -> sol::table {
                    auto &sel = SelectionManager::Instance();
                    sol::table t = lua.create_table();
                    t["has_selection"] = sel.HasSelection();
                    Model *m = sel.GetSelectedModel();
                    if (m)
                        t["model"] = sol::make_object(lua, m);
                    else
                        t["model"] = sol::nil;
                    t["node_index"] = sel.GetSelectedNodeIndex();
                    const char *typeStr = "none";
                    switch (sel.GetSelectionType())
                    {
                    case SelectionType::Node: typeStr = "node"; break;
                    case SelectionType::Mesh: typeStr = "mesh"; break;
                    case SelectionType::Camera: typeStr = "camera"; break;
                    case SelectionType::Light: typeStr = "light"; break;
                    case SelectionType::Emitter: typeStr = "emitter"; break;
                    }
                    t["type"] = typeStr;
                    return t;
                });

                selection.set_function("select_node", [](Model *model, int nodeIndex) {
                    if (!model) return;
                    SelectionManager::Instance().Select(model, nodeIndex, SelectionType::Node);
                });

                selection.set_function("select_mesh", [](Model *model, int nodeIndex) {
                    if (!model) return;
                    SelectionManager::Instance().Select(model, nodeIndex, SelectionType::Mesh);
                });

                selection.set_function("clear", []() {
                    SelectionManager::Instance().ClearSelection();
                });

                selection.set_function("get_gizmo", []() -> std::string {
                    switch (SelectionManager::Instance().GetGizmoOperation())
                    {
                    case GizmoOperation::Translate: return "translate";
                    case GizmoOperation::Rotate: return "rotate";
                    case GizmoOperation::Scale: return "scale";
                    default: return "translate";
                    }
                });

                selection.set_function("set_gizmo", [](const std::string &op) {
                    auto &sel = SelectionManager::Instance();
                    if (op == "translate") sel.SetGizmoOperation(GizmoOperation::Translate);
                    else if (op == "rotate") sel.SetGizmoOperation(GizmoOperation::Rotate);
                    else if (op == "scale") sel.SetGizmoOperation(GizmoOperation::Scale);
                });

                // Keep legacy global functions for backwards compatibility
                lua.set_function("save_scene", [](const std::string &name) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().SaveScene(Path::Assets + "Scenes/" + name);
                });

                lua.set_function("load_scene", [](const std::string &name) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().LoadScene(Path::Assets + "Scenes/" + name);
                }); });
        }
    } s_sceneBindings;
} // namespace pe
#endif
