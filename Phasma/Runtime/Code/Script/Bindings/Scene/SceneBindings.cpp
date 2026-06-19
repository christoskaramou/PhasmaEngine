#include "Script/ScriptSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneHost.h"
#include "Scene/SceneNode.h"
#include "Scene/SceneNodeHandle.h"
#include "Scene/Primitives.h"
#include "Camera/Camera.h"
#include "Script/ScriptRuntimeHooks.h"

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
                    SaveScene(Path::Assets + "Scenes/" + name);
                });

                scene.set_function("load", [](const std::string &name) {
                    LoadScene(Path::Assets + "Scenes/" + name);
                });

                scene.set_function("load_async", [](const std::string &name, sol::function callback) {
                    Scene *activeScene = GetActiveScene();
                    if (!activeScene) return;

                    // Clear scene immediately on main thread
                    NewScene();

                    SetScriptModelLoading(true);

                    std::string fullPath = Path::Assets + "Scenes/" + name;
                    uint32_t gen = activeScene->GetGeneration();

                    auto future = ThreadPool::General.Enqueue([fullPath]() -> ScenePreloadHandle * {
                        return new ScenePreloadHandle(PreloadScene(fullPath));
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
                    // Use NewScene for a complete, ordered cleanup — it nulls
                    // camera/light nodeIds before freeing nodes, clears all
                    // geometry stores, and rebuilds GPU buffers synchronously.
                    NewScene();
                });

                // Invoke a named action declared in the scene's script manifest (scene_scripts.actions).
                // The action's script is loaded lazily on first call. Returns true on success.
                scene.set_function("run_action", [](const std::string &id) -> bool {
                    auto *ss = GetGlobalSystem<ScriptSystem>();
                    return ss ? ss->InvokeSceneAction(id) : false;
                });

                scene.set_function("get_entities", [](sol::this_state ts) -> sol::as_table_t<std::vector<sol::table>> {
                    sol::state_view lua(ts);
                    std::vector<sol::table> result;
                    Scene *sc = GetActiveScene();
                    if (!sc) return sol::as_table(std::move(result));

                    for (uint32_t i = 0; i < sc->GetNodeCount(); i++)
                    {
                        NodeId *node = sc->GetNodeId(i);
                        if (sc->GetParent(node)) continue;
                        uint32_t flags = sc->GetComponentFlags(node);
                        if ((flags & Component_Camera) && !(flags & Component_Mesh)) continue;
                        if ((flags & Component_Light) && !(flags & Component_Mesh)) continue;

                        sol::table t = lua.create_table();
                        t["node"] = sc->MakeHandle(node);
                        t["label"] = sc->GetNodeName(node);
                        t["type"] = "node";
                        result.push_back(t);
                    }
                    return sol::as_table(std::move(result));
                });

                scene.set_function("get_model_count", []() -> int {
                    Scene *sc = GetActiveScene();
                    return sc ? static_cast<int>(sc->GetModels().size()) : 0;
                });

                scene.set_function("find_model", [](const std::string &label, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *sc = GetActiveScene();
                    if (!sc) return sol::make_object(lua, sol::nil);
                    for (uint32_t i = 0; i < sc->GetNodeCount(); i++)
                    {
                        NodeId *node = sc->GetNodeId(i);
                        if (sc->GetNodeName(node) == label)
                            return sol::make_object(lua, sc->MakeHandle(node));
                    }
                    return sol::make_object(lua, sol::nil);
                });

                scene.set_function("get_cameras", []() -> sol::as_table_t<std::vector<Camera *>> {
                    std::vector<Camera *> result;
                    Scene *sc = GetActiveScene();
                    if (sc)
                    {
                        for (auto *c : sc->GetCameras())
                            result.push_back(c);
                    }
                    return sol::as_table(std::move(result));
                });

                scene.set_function("get_active_camera", []() -> Camera * {
                    Scene *sc = GetActiveScene();
                    return sc ? sc->GetActiveCamera() : nullptr;
                });

                scene.set_function("add_camera", []() -> Camera * {
                    Scene *sc = GetActiveScene();
                    return sc ? sc->AddCamera() : nullptr;
                });

                scene.set_function("remove_camera", [](Camera *camera) {
                    if (!camera) return;
                    if (Scene *sc = GetActiveScene())
                        sc->RemoveCamera(camera);
                });

                scene.set_function("set_active_camera", [](Camera *camera) {
                    if (!camera) return;
                    if (Scene *sc = GetActiveScene())
                        sc->SetActiveCamera(camera);
                });

                scene.set_function("add_empty_node", [](sol::optional<std::string> name, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *sc = GetActiveScene();
                    if (!sc) return sol::make_object(lua, sol::nil);
                    std::string nodeName = name.value_or("Node_" + std::to_string(ID::NextID()));
                    NodeId *node = sc->CreateNode(nodeName);
                    return sol::make_object(lua, sc->MakeHandle(node));
                });

                scene.set_function("delete_node", [](SceneNodeHandle h) {
                    Scene *sc = GetActiveScene();
                    if (!sc) return;
                    if (h.IsValid(*sc))
                        sc->DeleteNode(h.nodeId);
                });

                // Instantiate a .peprefab subtree into the active scene, optionally
                // parented under an existing node. The path resolves against the
                // active project's Assets first, then the engine RuntimeAssets tree
                // (so "Prefabs/enemy.peprefab" works in any generated project), with
                // an as-given fallback for absolute / working-dir paths. Returns the
                // instance root node handle, or nil on failure.
                scene.set_function("instantiate_prefab", [](const std::string &path, sol::optional<SceneNodeHandle> parent, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *sc = GetActiveScene();
                    if (!sc) return sol::make_object(lua, sol::nil);

                    std::error_code ec;
                    std::filesystem::path resolved = path;
                    if (!std::filesystem::exists(resolved, ec))
                        resolved = Path::ResolveAsset(path);

                    NodeId *parentNode = (parent && parent->IsValid(*sc)) ? parent->nodeId : nullptr;
                    SceneNodeHandle handle = sc->InstantiatePrefab(resolved, parentNode);
                    if (!handle.nodeId)
                        return sol::make_object(lua, sol::nil);
                    return sol::make_object(lua, handle);
                });

                scene.set_function("attach_primitive", [](SceneNodeHandle h, const std::string &type) {
                    Scene *sc = GetActiveScene();
                    if (!sc) return;
                    if (!h.IsValid(*sc)) return;
                    ModelAsset *model = nullptr;
                    if (type == "plane") model = Primitives::CreatePlane();
                    else if (type == "grid") model = Primitives::CreateGrid();
                    else if (type == "cube") model = Primitives::CreateCube();
                    else if (type == "sphere") model = Primitives::CreateSphere();
                    else if (type == "uv_sphere") model = Primitives::CreateUvSphere();
                    else if (type == "ico_sphere") model = Primitives::CreateIcoSphere();
                    else if (type == "cylinder") model = Primitives::CreateCylinder();
                    else if (type == "cone") model = Primitives::CreateCone();
                    else if (type == "pyramid") model = Primitives::CreatePyramid();
                    else if (type == "quad") model = Primitives::CreateQuad();
                    else if (type == "circle") model = Primitives::CreateCircle();
                    else if (type == "torus") model = Primitives::CreateTorus();
                    else if (type == "skinned_strip_2d") model = Primitives::CreateSkinnedStrip2D();
                    if (model)
                    {
                        sc->AttachPrimitiveToNode(h.nodeId, model);
                        sc->SetGeometryDirty();
                    }
                });

                scene.set_function("attach_polyline", [](SceneNodeHandle h, sol::table pointsTable, sol::optional<float> width, sol::optional<vec3> normal, sol::optional<bool> closed) {
                    Scene *sc = GetActiveScene();
                    if (!sc) return;
                    if (!h.IsValid(*sc)) return;
                    std::vector<vec3> points;
                    points.reserve(pointsTable.size());
                    for (size_t i = 1; i <= pointsTable.size(); ++i)
                    {
                        sol::object p = pointsTable[i];
                        if (p.is<vec3>())
                            points.push_back(p.as<vec3>());
                    }
                    ModelAsset *model = Primitives::CreatePolylineRibbon(points,
                                                                         width.value_or(1.0f),
                                                                         normal.value_or(vec3(0.0f, 1.0f, 0.0f)),
                                                                         closed.value_or(true));
                    if (model)
                    {
                        sc->AttachPrimitiveToNode(h.nodeId, model);
                        sc->SetGeometryDirty();
                    }
                });

                // attach_lines(node, {vec3...}, closed): hardware line strip drawn by
                // LinesPass - screen-constant 1px width, visible from any angle and
                // distance. Color comes from the material emissive (or base_color).
                scene.set_function("attach_lines", [](SceneNodeHandle h, sol::table pointsTable, sol::optional<bool> closed) {
                    Scene *sc = GetActiveScene();
                    if (!sc) return;
                    if (!h.IsValid(*sc)) return;
                    std::vector<vec3> points;
                    points.reserve(pointsTable.size());
                    for (size_t i = 1; i <= pointsTable.size(); ++i)
                    {
                        sol::object p = pointsTable[i];
                        if (p.is<vec3>())
                            points.push_back(p.as<vec3>());
                    }
                    ModelAsset *model = Primitives::CreatePolyline(points, closed.value_or(true));
                    if (model)
                    {
                        sc->AttachPrimitiveToNode(h.nodeId, model);
                        sc->SetGeometryDirty();
                    }
                });

                scene.set_function("add_directional_light", []() {
                    if (Scene *sc = GetActiveScene()) sc->CreateDirectionalLight();
                });

                scene.set_function("add_point_light", []() {
                    if (Scene *sc = GetActiveScene()) sc->CreatePointLight();
                });

                scene.set_function("add_spot_light", []() {
                    if (Scene *sc = GetActiveScene()) sc->CreateSpotLight();
                });

                scene.set_function("add_area_light", []() {
                    if (Scene *sc = GetActiveScene()) sc->CreateAreaLight();
                });

                scene.set_function("remove_light", [](const std::string &type, int index) {
                    Scene *sc = GetActiveScene();
                    if (!sc) return;
                    LightType lt;
                    if (type == "directional") lt = LightType::Directional;
                    else if (type == "point") lt = LightType::Point;
                    else if (type == "spot") lt = LightType::Spot;
                    else if (type == "area") lt = LightType::Area;
                    else { PE_WARN("remove_light: unknown type '%s'", type.c_str()); return; }
                    sc->RemoveLight(lt, index);
                });

                // Keep legacy global functions for backwards compatibility
                lua.set_function("save_scene", [](const std::string &name) {
                    SaveScene(Path::Assets + "Scenes/" + name);
                }); });
        }
    } s_sceneBindings;
} // namespace pe
