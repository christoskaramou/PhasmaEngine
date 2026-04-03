#include "Script/ScriptSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scene/SceneNodeHandle.h"
#include "Scene/SelectionManager.h"
#include "Camera/Camera.h"
#include "Systems/RendererSystem.h"
#include "GUI/GUIState.h"
#include "Base/ThreadPool.h"

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
                    if (!r) return;
                    r->WaitAllFramesCommands();
                    r->GetScene().LoadScene(Path::Assets + "Scenes/" + name);
                });

                scene.set_function("load_async", [](const std::string &name, sol::function callback) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;

                    // Clear scene immediately on main thread
                    r->WaitAllFramesCommands();
                    r->GetScene().NewScene();

                    GUIState::s_modelLoading = true;

                    std::string fullPath = Path::Assets + "Scenes/" + name;
                    uint32_t gen = r->GetScene().GetGeneration();

                    auto future = ThreadPool::General.Enqueue([fullPath]() -> Scene::ScenePreload * {
                        return new Scene::ScenePreload(Scene::PreloadScene(fullPath));
                    });

                    auto *ss = GetGlobalSystem<ScriptSystem>();
                    if (ss)
                    {
                        PendingSceneLoad load;
                        load.future = std::move(future);
                        load.callback = std::move(callback);
                        load.sceneGeneration = gen;
                        ss->AddPendingSceneLoad(std::move(load));
                    }
                });

                scene.set_function("clear", []() {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r)
                        return;

                    // Wait for in-flight GPU work before tearing down the scene.
                    r->WaitAllFramesCommands();

                    // Use NewScene for a complete, ordered cleanup — it nulls
                    // camera/light nodeIds before freeing nodes, clears all
                    // geometry stores, and rebuilds GPU buffers synchronously.
                    r->GetScene().NewScene();
                });

                scene.set_function("get_entities", [](sol::this_state ts) -> sol::as_table_t<std::vector<sol::table>> {
                    sol::state_view lua(ts);
                    std::vector<sol::table> result;
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return sol::as_table(std::move(result));

                    Scene &sc = r->GetScene();
                    for (uint32_t i = 0; i < sc.GetNodeCount(); i++)
                    {
                        NodeId *node = sc.GetNodeId(i);
                        if (sc.GetParent(node)) continue;
                        uint32_t flags = sc.GetComponentFlags(node);
                        if ((flags & Component_Camera) && !(flags & Component_Mesh)) continue;
                        if ((flags & Component_Light) && !(flags & Component_Mesh)) continue;

                        sol::table t = lua.create_table();
                        t["node"] = sc.MakeHandle(node);
                        t["label"] = sc.GetNodeName(node);
                        t["type"] = "node";
                        result.push_back(t);
                    }
                    return sol::as_table(std::move(result));
                });

                scene.set_function("get_model_count", []() -> int {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    return r ? static_cast<int>(r->GetScene().GetModels().size()) : 0;
                });

                scene.set_function("find_model", [](const std::string &label, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return sol::make_object(lua, sol::nil);
                    Scene &sc = r->GetScene();
                    for (uint32_t i = 0; i < sc.GetNodeCount(); i++)
                    {
                        NodeId *node = sc.GetNodeId(i);
                        if (sc.GetNodeName(node) == label)
                            return sol::make_object(lua, sc.MakeHandle(node));
                    }
                    return sol::make_object(lua, sol::nil);
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

                scene.set_function("add_empty_node", [](sol::optional<std::string> name, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return sol::make_object(lua, sol::nil);
                    Scene &sc = r->GetScene();
                    std::string nodeName = name.value_or("Node_" + std::to_string(ID::NextID()));
                    NodeId *node = sc.CreateNode(nodeName);
                    return sol::make_object(lua, sc.MakeHandle(node));
                });

                scene.set_function("delete_node", [](SceneNodeHandle h) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;
                    Scene &sc = r->GetScene();
                    if (h.IsValid(sc))
                        sc.DeleteNode(h.nodeId);
                });

                scene.set_function("add_directional_light", []() {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().CreateDirectionalLight();
                });

                scene.set_function("add_point_light", []() {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().CreatePointLight();
                });

                scene.set_function("add_spot_light", []() {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().CreateSpotLight();
                });

                scene.set_function("add_area_light", []() {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (r) r->GetScene().CreateAreaLight();
                });

                scene.set_function("remove_light", [](const std::string &type, int index) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;
                    LightType lt;
                    if (type == "directional") lt = LightType::Directional;
                    else if (type == "point") lt = LightType::Point;
                    else if (type == "spot") lt = LightType::Spot;
                    else if (type == "area") lt = LightType::Area;
                    else { PE_WARN("remove_light: unknown type '%s'", type.c_str()); return; }
                    r->GetScene().RemoveLight(lt, index);
                });

                // Selection
                sol::table selection = lua.create_named_table("selection");

                selection.set_function("get", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    auto &sel = SelectionManager::Instance();
                    sol::table t = lua.create_table();
                    t["has_selection"] = sel.HasSelection();
                    NodeId *node = sel.GetSelectedNode();
                    if (node)
                        t["node_index"] = node->index;
                    else
                        t["node_index"] = sol::nil;
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

                selection.set_function("select_node", [](int nodeIndex) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;
                    Scene &scene = r->GetScene();
                    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(scene.GetNodeCount())) return;
                    SelectionManager::Instance().Select(scene.GetNodeId(nodeIndex), SelectionType::Node);
                });

                selection.set_function("select_mesh", [](int nodeIndex) {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;
                    Scene &scene = r->GetScene();
                    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(scene.GetNodeCount())) return;
                    SelectionManager::Instance().Select(scene.GetNodeId(nodeIndex), SelectionType::Mesh);
                });

                selection.set_function("clear", []() {
                    SelectionManager::Instance().ClearSelection();
                });

                // Focus the active camera on the currently selected object.
                selection.set_function("focus", []() {
                    auto *r = GetGlobalSystem<RendererSystem>();
                    if (!r) return;
                    Camera *cam = r->GetScene().GetActiveCamera();
                    if (!cam) return;

                    auto &sel = SelectionManager::Instance();
                    vec3 center;
                    float radius = 1.0f;

                    if (sel.GetSelectionType() == SelectionType::Node ||
                        sel.GetSelectionType() == SelectionType::Mesh)
                    {
                        NodeId *node = sel.GetSelectedNode();
                        if (!node) return;
                        const AABB &bb = r->GetScene().GetWorldAABB(node);
                        center = bb.GetCenter();
                        radius = glm::length(bb.GetSize()) * 0.5f;
                    }
                    else if (sel.GetSelectionType() == SelectionType::Light)
                    {
                        auto &scene = r->GetScene();
                        int idx = sel.GetSelectedLightIndex();
                        if (sel.GetSelectedLightType() == LightType::Point)
                        {
                            auto &lights = scene.GetPointLights();
                            if (idx < 0 || idx >= static_cast<int>(lights.size())) return;
                            center = vec3(lights[idx].position);
                        }
                        else
                        {
                            auto &lights = scene.GetDirectionalLights();
                            if (idx < 0 || idx >= static_cast<int>(lights.size())) return;
                            center = vec3(lights[idx].position);
                        }
                    }
                    else
                        return;

                    vec3 dir = glm::normalize(cam->GetFront());
                    cam->SetPosition(center - dir * (radius * 2.0f));
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
                }); });
        }
    } s_sceneBindings;
} // namespace pe
