#include "Script/ScriptSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneHost.h"
#include "Scene/SceneNode.h"
#include "Scene/SceneNodeHandle.h"
#include "Scene/Primitives.h"
#include "Camera/Camera.h"
#include "Script/ScriptRuntimeHooks.h"
#include "Render/SceneRendererHost.h"
#include "Render/ScenePerception.h"

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

                // Batched transform writes: one Lua->C++ crossing for N nodes instead of N
                // usertype calls. `t` is a flat interleaved array {node, x, y, z, ...} reused
                // as a scratch table by callers; `count` is the number of node entries (the
                // table may have a stale tail beyond count*4). Invalid/stale handles are
                // skipped, so callers don't need a separate is_valid crossing per node.
                scene.set_function("set_positions", [](sol::table t, int count) {
                    Scene *s = GetActiveScene();
                    if (!s) return;
                    for (int i = 0; i < count; i++)
                    {
                        int base = i * 4;
                        auto h = t.raw_get<sol::optional<SceneNodeHandle>>(base + 1);
                        auto x = t.raw_get<sol::optional<float>>(base + 2);
                        auto y = t.raw_get<sol::optional<float>>(base + 3);
                        auto z = t.raw_get<sol::optional<float>>(base + 4);
                        if (!h || !x || !y || !z || !h->IsValid(*s))
                            continue;
                        mat4 local = s->GetLocalMatrix(h->nodeId);
                        local[3] = vec4(*x, *y, *z, 1.0f);
                        s->SetLocalMatrix(h->nodeId, local);
                    }
                });

                // Same contract as set_positions; rotation is euler degrees, matching
                // node:set_rotation (decompose local matrix, rebuild T*R*S).
                scene.set_function("set_rotations", [](sol::table t, int count) {
                    Scene *s = GetActiveScene();
                    if (!s) return;
                    for (int i = 0; i < count; i++)
                    {
                        int base = i * 4;
                        auto h = t.raw_get<sol::optional<SceneNodeHandle>>(base + 1);
                        auto rx = t.raw_get<sol::optional<float>>(base + 2);
                        auto ry = t.raw_get<sol::optional<float>>(base + 3);
                        auto rz = t.raw_get<sol::optional<float>>(base + 4);
                        if (!h || !rx || !ry || !rz || !h->IsValid(*s))
                            continue;
                        const mat4 &m = s->GetLocalMatrix(h->nodeId);
                        vec3 pos(m[3]);
                        vec3 sc(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
                        mat4 rot = glm::mat4_cast(glm::quat(glm::radians(vec3(*rx, *ry, *rz))));
                        mat4 local = glm::translate(mat4(1.f), pos) * rot * glm::scale(mat4(1.f), sc);
                        s->SetLocalMatrix(h->nodeId, local);
                    }
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

                // Spatial perception: world bounds + every mesh node's world AABB +
                // overlaps, so authoring scripts can place/frame without trial-and-error.
                // world_bounds/ground_y/overlaps consider enabled+visible nodes only
                // (parked pools excluded); overlaps skip flat-in-Y floors and props resting on a
                // larger footprint (embedded/co-located pairs still report). Shares
                // ComputeSceneDigest with MCP get_scene_digest.
                scene.set_function("digest", [](sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *sc = GetActiveScene();
                    if (!sc) return sol::make_object(lua, sol::nil);

                    SceneDigest d = ComputeSceneDigest(*sc);
                    sol::table out = lua.create_table();
                    out["ground_y"] = d.groundY;
                    out["total_node_count"] = d.totalNodeCount;
                    out["mesh_node_count"] = d.meshNodeCount;
                    out["overlaps_truncated"] = d.overlapsTruncated;
                    if (d.hasBounds)
                    {
                        out["world_bounds"] = lua.create_table_with(
                            "min", d.worldBounds.min, "max", d.worldBounds.max,
                            "center", d.worldBounds.GetCenter(), "size", d.worldBounds.GetSize());
                    }

                    sol::table nodes = lua.create_table();
                    for (size_t i = 0; i < d.nodes.size(); ++i)
                    {
                        const SceneDigestNode &n = d.nodes[i];
                        sol::table t = lua.create_table();
                        t["id"] = n.id;
                        t["name"] = n.name;
                        t["enabled"] = n.enabled;
                        t["visible"] = n.visible;
                        t["in_frustum"] = n.inFrustum;
                        t["ground_outlier"] = n.groundOutlier;
                        t["parent_name"] = n.parentName;
                        t["aabb"] = lua.create_table_with(
                            "min", n.aabb.min, "max", n.aabb.max,
                            "center", n.aabb.GetCenter(), "size", n.aabb.GetSize());
                        nodes[i + 1] = t;
                    }
                    out["nodes"] = nodes;

                    sol::table overlaps = lua.create_table();
                    for (size_t i = 0; i < d.overlaps.size(); ++i)
                    {
                        const auto &pair = d.overlaps[i];
                        sol::table t = lua.create_table();
                        t["a"] = pair.first + 1; // 1-based index into nodes
                        t["b"] = pair.second + 1;
                        t["a_name"] = d.nodes[pair.first].name;
                        t["b_name"] = d.nodes[pair.second].name;
                        overlaps[i + 1] = t;
                    }
                    out["overlaps"] = overlaps;

                    return sol::make_object(lua, out);
                });

                // Eye-level spatial perception: occlusion-exact set of nodes the ACTIVE camera sees,
                // each with a node handle, exact visible pixel count, screen bbox, nearest NDC depth
                // and camera-space distance (nil when infinitely far). Aim the camera first (move the
                // camera node / set_active_camera). Shares pe::DecodeCameraView with the MCP
                // decode_camera_view tool. Runs a one-off GPU pass + readback — an authoring/debug
                // call, not a per-frame one. Returns nil if no renderer/camera. Optional min_pixels.
                scene.set_function("decode_view", [](sol::optional<int> minPixelsOpt, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *sc = GetActiveScene();
                    SceneRendererHost *host = GetActiveSceneRendererHost();
                    if (!sc || !host) return sol::make_object(lua, sol::nil);

                    const uint32_t minPixels = static_cast<uint32_t>(std::max(0, minPixelsOpt.value_or(1)));
                    Image *depth = host->GetDepthStencilTarget("depthStencil");
                    CameraViewResult r = DecodeCameraView(*sc, depth, minPixels);
                    if (!r.valid) return sol::make_object(lua, sol::nil);

                    sol::table out = lua.create_table();
                    out["width"] = r.width;
                    out["height"] = r.height;
                    sol::table nodes = lua.create_table();
                    int idx = 1;
                    for (const CameraViewNodeVis &n : r.nodes)
                    {
                        NodeId *node = (n.nodeIndex >= 0 && n.nodeIndex < static_cast<int>(sc->GetNodeCount()))
                                           ? sc->GetNodeId(static_cast<uint32_t>(n.nodeIndex)) : nullptr;
                        if (!node) continue;
                        sol::table t = lua.create_table();
                        t["node"] = sc->MakeHandle(node);
                        t["name"] = sc->GetNodeName(node);
                        t["visible_pixels"] = static_cast<double>(n.visiblePixels);
                        t["nearest_ndc_depth"] = n.nearestNdcDepth;
                        if (std::isfinite(n.distance)) t["distance"] = n.distance;
                        t["screen_box"] = lua.create_table_with("min_x", n.minX, "min_y", n.minY, "max_x", n.maxX, "max_y", n.maxY);
                        nodes[idx++] = t;
                    }
                    out["nodes"] = nodes;
                    return sol::make_object(lua, out);
                });

                // Perspective pixel pick (the eye-level analogue of the top-down map pick): the exact
                // node + world point under pixel (x,y) of the decode_view image. hit=true -> node handle
                // + world_hit (exact unprojected surface point) + distance; hit=false -> ground_point
                // (camera ray intersected with Y=ground_y, default 0) when it crosses the plane. Shares
                // pe::PickCameraPixel with the MCP pick_camera_point tool. Returns nil if no renderer/camera.
                scene.set_function("pick_view_pixel", [](int x, int y, sol::optional<float> groundYOpt, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *sc = GetActiveScene();
                    SceneRendererHost *host = GetActiveSceneRendererHost();
                    if (!sc || !host) return sol::make_object(lua, sol::nil);

                    Image *depth = host->GetDepthStencilTarget("depthStencil");
                    CameraPickResult r = PickCameraPixel(*sc, depth, x, y, groundYOpt.value_or(0.0f));
                    if (!r.valid) return sol::make_object(lua, sol::nil);

                    sol::table out = lua.create_table();
                    out["hit"] = r.hit;
                    out["width"] = r.width;
                    out["height"] = r.height;
                    if (r.hit)
                    {
                        out["world_hit"] = r.worldHit;
                        out["nearest_ndc_depth"] = r.ndcDepth;
                        if (std::isfinite(r.distance)) out["distance"] = r.distance;
                        if (r.nodeIndex >= 0 && r.nodeIndex < static_cast<int>(sc->GetNodeCount()))
                        {
                            if (NodeId *node = sc->GetNodeId(static_cast<uint32_t>(r.nodeIndex)))
                            {
                                out["node"] = sc->MakeHandle(node);
                                out["name"] = sc->GetNodeName(node);
                            }
                        }
                    }
                    else if (r.hasGroundPoint)
                    {
                        out["ground_point"] = r.groundPoint;
                    }
                    return sol::make_object(lua, out);
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

                    std::filesystem::path resolved = path;
                    if (!AssetFileExists(resolved))
                        resolved = Path::ResolveAsset(path);

                    NodeId *parentNode = (parent && parent->IsValid(*sc)) ? parent->nodeId : nullptr;
                    SceneNodeHandle handle = sc->InstantiatePrefab(resolved, parentNode);
                    if (!handle.nodeId)
                        return sol::make_object(lua, sol::nil);
                    return sol::make_object(lua, handle);
                });

                // Save a node's subtree as a reusable .peprefab. A relative path resolves against
                // the active project's Assets/ (so "Prefabs/turret.peprefab" lands in the project,
                // ready for scene.instantiate_prefab); absolute paths pass through. The ".peprefab"
                // extension is added if omitted. Returns true on success. This closes the authoring
                // loop: build a subtree with gamekit/build, save_prefab it, then stamp copies with
                // instantiate_prefab.
                scene.set_function("save_prefab", [](SceneNodeHandle root, const std::string &path) -> bool {
                    Scene *sc = GetActiveScene();
                    if (!sc || !root.IsValid(*sc))
                        return false;
                    std::filesystem::path out = path;
                    if (!out.is_absolute())
                        out = std::filesystem::path(Path::Assets) / out;
                    return sc->SavePrefab(root.nodeId, out);
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
                        sc->AttachPrimitiveToNode(h.nodeId, model);
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
                        sc->AttachPrimitiveToNode(h.nodeId, model);
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
                        sc->AttachPrimitiveToNode(h.nodeId, model);
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
