#include "Script/ScriptSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNode.h"
#include "Scene/SceneNodeHandle.h"
#include "Scene/NodeComponents.h"
#include "UI/RuntimeUi.h"
#include "Camera/Camera.h"

namespace pe
{
    static Scene *GetScene()
    {
        return GetActiveScene();
    }

    static mat4 ComputeNodeWorldMatrix(Scene &scene, NodeId *node)
    {
        if (!node || !scene.IsNodeAlive(node))
            return mat4(1.0f);

        NodeId *parent = scene.GetParent(node);
        if (!parent)
            return scene.GetLocalMatrix(node);

        return ComputeNodeWorldMatrix(scene, parent) * scene.GetLocalMatrix(node);
    }

    static vec3 MatrixScale(const mat4 &m)
    {
        return vec3(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
    }

    static struct SceneNodeBindings
    {
        SceneNodeBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                auto ut = lua.new_usertype<SceneNodeHandle>("SceneNode", sol::no_constructor);

                ut[sol::meta_function::to_string] = [](SceneNodeHandle &h) -> std::string {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return "<invalid SceneNode>";
                    return s->GetNodeName(h.nodeId);
                };

                ut[sol::meta_function::equal_to] = [](const SceneNodeHandle &a, const SceneNodeHandle &b) {
                    return a == b;
                };

                ut.set_function("is_valid", [](SceneNodeHandle &h) -> bool {
                    Scene *s = GetScene();
                    return s && h.IsValid(*s);
                });

                ut.set_function("is_ready", [](SceneNodeHandle &h) -> bool {
                    Scene *s = GetScene();
                    return s && h.IsReady(*s);
                });

                ut.set_function("is_enabled", [](SceneNodeHandle &h) -> bool {
                    Scene *s = GetScene();
                    return s && h.IsValid(*s) && s->IsNodeEnabled(h.nodeId);
                });

                ut.set_function("is_hierarchy_enabled", [](SceneNodeHandle &h) -> bool {
                    Scene *s = GetScene();
                    return s && h.IsValid(*s) && s->IsNodeHierarchyEnabled(h.nodeId);
                });

                ut.set_function("set_enabled", [](SceneNodeHandle &h, bool enabled) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->SetNodeEnabled(h.nodeId, enabled);
                });

                ut.set_function("get_name", [](SceneNodeHandle &h) -> std::string {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return "";
                    return s->GetNodeName(h.nodeId);
                });

                ut.set_function("get_index", [](SceneNodeHandle &h) -> int {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return -1;
                    return static_cast<int>(h.nodeId->index);
                });

                ut.set_function("is_runtime_ui", [](SceneNodeHandle &h) -> bool {
                    Scene *s = GetScene();
                    return s && h.IsValid(*s) && (s->GetComponentFlags(h.nodeId) & Component_RuntimeUi) != 0;
                });

                ut.set_function("set_runtime_ui", [](SceneNodeHandle &h, bool enabled) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    if (enabled)
                        s->AddComponentFlag(h.nodeId, Component_RuntimeUi);
                    else
                        s->RemoveComponentFlag(h.nodeId, Component_RuntimeUi);
                });

                ut.set_function("set_name", [](SceneNodeHandle &h, const std::string &name) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->SetNodeName(h.nodeId, name);
                });

                // Update an authored Runtime UI node's content/style at runtime. The
                // node carries the real (static) text/colors authored in the editor;
                // a script attached to it calls node:set_ui{...} to drive the dynamic
                // parts (e.g. FPS number, "HERO 152/155"). SyncSceneWidgets re-reads the
                // tag every frame, so mutations show next frame. Accepts any of:
                //   text|body|title|subtitle|footer|label = string
                //   image = string (path)
                //   fill|border|accent|text_color|image_tint = {r,g,b,a} or {1,2,3,4}
                //   font_scale = number, visible = bool
                ut.set_function("set_ui", [](SceneNodeHandle &h, sol::table opts) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    NodeRuntimeUiTag *ui = s->GetRuntimeUiComponent(h.nodeId);
                    if (!ui) return; // node has no authored runtime_ui tag

                    auto readStr = [&](const char *key, std::string &dst) {
                        sol::object v = opts[key];
                        if (v.is<std::string>()) dst = v.as<std::string>();
                    };
                    readStr("text", ui->body); // convenience alias for the common Text widget
                    readStr("body", ui->body);
                    readStr("title", ui->title);
                    readStr("subtitle", ui->subtitle);
                    readStr("footer", ui->footer);
                    readStr("label", ui->label);
                    readStr("image", ui->imagePath);

                    auto readColor = [&](const char *key, vec4 &dst) {
                        sol::object v = opts[key];
                        if (!v.is<sol::table>()) return;
                        sol::table c = v.as<sol::table>();
                        auto comp = [&](const char *nk, int ik, float cur) -> float {
                            sol::object o = c[nk];
                            if (o.is<double>()) return static_cast<float>(o.as<double>());
                            sol::object oi = c[ik];
                            if (oi.is<double>()) return static_cast<float>(oi.as<double>());
                            return cur;
                        };
                        dst.r = comp("r", 1, dst.r);
                        dst.g = comp("g", 2, dst.g);
                        dst.b = comp("b", 3, dst.b);
                        dst.a = comp("a", 4, dst.a);
                    };
                    readColor("fill", ui->fillColor);
                    readColor("border", ui->borderColor);
                    readColor("accent", ui->accentColor);
                    readColor("text_color", ui->textColor);
                    readColor("image_tint", ui->imageTint);

                    sol::object fontScale = opts["font_scale"];
                    if (fontScale.is<double>()) ui->fontScale = static_cast<float>(fontScale.as<double>());
                    sol::object visible = opts["visible"];
                    if (visible.is<bool>()) ui->visible = visible.as<bool>();
                });

                // Rendered screen rect of an authored UI node (anchor+pivot+surface
                // applied), as {x,y,w,h} in surface pixels, or nil if not laid out.
                // Use this (not get_world_position) when a script draws relative to a
                // UI node, since the node translation is only the anchor offset.
                ut.set_function("get_ui_rect", [](SceneNodeHandle &h, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return sol::make_object(ts, sol::nil);
                    RuntimeUiSystem *rui = GetActiveRuntimeUi();
                    if (!rui) return sol::make_object(ts, sol::nil);
                    float x = 0.0f, y = 0.0f, w = 0.0f, rh = 0.0f;
                    if (!rui->GetNodeRect(h.nodeId, x, y, w, rh)) return sol::make_object(ts, sol::nil);
                    sol::table t = lua.create_table();
                    t["x"] = x;
                    t["y"] = y;
                    t["w"] = w;
                    t["h"] = rh;
                    return sol::make_object(ts, t);
                });

                ut.set_function("get_position", [](SceneNodeHandle &h) -> vec3 {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return vec3(0.f);
                    return vec3(s->GetLocalMatrix(h.nodeId)[3]);
                });

                ut.set_function("get_world_position", [](SceneNodeHandle &h) -> vec3 {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return vec3(0.f);
                    return vec3(ComputeNodeWorldMatrix(*s, h.nodeId)[3]);
                });

                ut.set_function("set_position", [](SceneNodeHandle &h, const vec3 &pos) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    mat4 m = s->GetLocalMatrix(h.nodeId);
                    m[3] = vec4(pos, 1.0f);
                    s->SetLocalMatrix(h.nodeId, m);
                });

                ut.set_function("get_rotation", [](SceneNodeHandle &h) -> vec3 {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return vec3(0.f);
                    const mat4 &m = s->GetLocalMatrix(h.nodeId);
                    vec3 scale(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
                    mat3 rotMat(vec3(m[0]) / scale.x, vec3(m[1]) / scale.y, vec3(m[2]) / scale.z);
                    return glm::degrees(glm::eulerAngles(glm::quat_cast(rotMat)));
                });

                ut.set_function("set_rotation", [](SceneNodeHandle &h, const vec3 &rot_deg) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    const mat4 &m = s->GetLocalMatrix(h.nodeId);
                    vec3 pos(m[3]);
                    vec3 sc(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
                    mat4 T = glm::translate(mat4(1.0f), pos);
                    mat4 R = glm::mat4_cast(glm::quat(glm::radians(rot_deg)));
                    mat4 S = glm::scale(mat4(1.0f), sc);
                    s->SetLocalMatrix(h.nodeId, T * R * S);
                });

                ut.set_function("get_scale", [](SceneNodeHandle &h) -> vec3 {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return vec3(1.f);
                    return MatrixScale(s->GetLocalMatrix(h.nodeId));
                });

                ut.set_function("get_world_scale", [](SceneNodeHandle &h) -> vec3 {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return vec3(1.f);
                    return MatrixScale(ComputeNodeWorldMatrix(*s, h.nodeId));
                });

                ut.set_function("set_scale", [](SceneNodeHandle &h, const vec3 &newScale) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    const mat4 &m = s->GetLocalMatrix(h.nodeId);
                    vec3 pos(m[3]);
                    vec3 oldScale(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
                    mat3 rotMat(vec3(m[0]) / oldScale.x, vec3(m[1]) / oldScale.y, vec3(m[2]) / oldScale.z);
                    mat4 T = glm::translate(mat4(1.0f), pos);
                    mat4 R = glm::mat4_cast(glm::quat_cast(rotMat));
                    mat4 S = glm::scale(mat4(1.0f), newScale);
                    s->SetLocalMatrix(h.nodeId, T * R * S);
                });

                ut.set_function("set_transform", [](SceneNodeHandle &h, const vec3 &pos, const vec3 &rot_deg, const vec3 &scale) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    mat4 T = glm::translate(mat4(1.0f), pos);
                    mat4 R = glm::mat4_cast(glm::quat(glm::radians(rot_deg)));
                    mat4 S = glm::scale(mat4(1.0f), scale);
                    s->SetLocalMatrix(h.nodeId, T * R * S);
                });

                ut.set_function("get_parent", [](SceneNodeHandle &h, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return sol::make_object(lua, sol::nil);
                    NodeId *parent = s->GetParent(h.nodeId);
                    if (!parent) return sol::make_object(lua, sol::nil);
                    return sol::make_object(lua, s->MakeHandle(parent));
                });

                ut.set_function("get_children", [](SceneNodeHandle &h) -> sol::as_table_t<std::vector<SceneNodeHandle>> {
                    std::vector<SceneNodeHandle> result;
                    Scene *s = GetScene();
                    if (s && h.IsValid(*s))
                    {
                        for (NodeId *child : s->GetChildren(h.nodeId))
                            result.push_back(s->MakeHandle(child));
                    }
                    return sol::as_table(std::move(result));
                });

                ut.set_function("get_mesh_index", sol::overload(
                    [](SceneNodeHandle &h) -> int {
                        Scene *s = GetScene();
                        if (!s || !h.IsValid(*s)) return -1;
                        return s->GetMeshRef(h.nodeId);
                    },
                    [](SceneNodeHandle &h, int slot) -> int {
                        Scene *s = GetScene();
                        if (!s || !h.IsValid(*s)) return -1;
                        const auto &refs = s->GetNodeCache(h.nodeId).meshRefs->meshRefs;
                        if (slot < 0 || slot >= static_cast<int>(refs.size())) return -1;
                        return refs[slot];
                    }));

                ut.set_function("get_mesh_count", [](SceneNodeHandle &h) -> int {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return 0;
                    return static_cast<int>(s->GetNodeCache(h.nodeId).meshRefs->meshRefs.size());
                });

                ut.set_function("get_mesh_info", sol::overload(
                    [](SceneNodeHandle &h, sol::this_state ts) -> sol::object {
                        sol::state_view lua(ts);
                        Scene *s = GetScene();
                        if (!s || !h.IsValid(*s)) return sol::make_object(lua, sol::nil);
                        int meshRef = s->GetMeshRef(h.nodeId);
                        if (meshRef < 0) return sol::make_object(lua, sol::nil);
                        const auto &mesh = s->GetMeshes()[meshRef];
                        sol::table t = lua.create_table();
                        t["index"] = meshRef;
                        t["vertex_count"] = mesh.vertexCount;
                        t["index_count"] = mesh.indexCount;
                        t["bounding_box"] = lua.create_table_with(
                            "min", mesh.boundingBox.min, "max", mesh.boundingBox.max,
                            "center", mesh.boundingBox.GetCenter(), "size", mesh.boundingBox.GetSize());
                        return sol::make_object(lua, t);
                    },
                    [](SceneNodeHandle &h, int slot, sol::this_state ts) -> sol::object {
                        sol::state_view lua(ts);
                        Scene *s = GetScene();
                        if (!s || !h.IsValid(*s)) return sol::make_object(lua, sol::nil);
                        const auto &refs = s->GetNodeCache(h.nodeId).meshRefs->meshRefs;
                        if (slot < 0 || slot >= static_cast<int>(refs.size())) return sol::make_object(lua, sol::nil);
                        int meshRef = refs[slot];
                        if (meshRef < 0) return sol::make_object(lua, sol::nil);
                        const auto &mesh = s->GetMeshes()[meshRef];
                        sol::table t = lua.create_table();
                        t["index"] = meshRef;
                        t["slot"] = slot;
                        t["vertex_count"] = mesh.vertexCount;
                        t["index_count"] = mesh.indexCount;
                        t["bounding_box"] = lua.create_table_with(
                            "min", mesh.boundingBox.min, "max", mesh.boundingBox.max,
                            "center", mesh.boundingBox.GetCenter(), "size", mesh.boundingBox.GetSize());
                        return sol::make_object(lua, t);
                    }));

                ut.set_function("get_camera", [](SceneNodeHandle &h, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return sol::make_object(lua, sol::nil);
                    if (!(s->GetComponentFlags(h.nodeId) & Component_Camera))
                        return sol::make_object(lua, sol::nil);
                    Camera *cam = s->GetCameraForNode(h.nodeId);
                    if (!cam) return sol::make_object(lua, sol::nil);
                    return sol::make_object(lua, cam);
                });

                ut.set_function("get_bounding_box", [](SceneNodeHandle &h, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return sol::make_object(lua, sol::nil);
                    const AABB &bb = s->GetWorldAABB(h.nodeId);
                    sol::table t = lua.create_table();
                    t["min"] = bb.min;
                    t["max"] = bb.max;
                    t["center"] = bb.GetCenter();
                    t["size"] = bb.GetSize();
                    return sol::make_object(lua, t);
                });

                ut.set_function("set_script", [](SceneNodeHandle &h, const std::string &path) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->SetNodeScript(h.nodeId, path);
                });

                ut.set_function("get_script", [](SceneNodeHandle &h) -> std::string {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return "";
                    return s->GetNodeScriptPath(h.nodeId);
                });

                ut.set_function("remove", [](SceneNodeHandle &h) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->DeleteNode(h.nodeId);
                });

                ut.set_function("set_parent", [](SceneNodeHandle &h, sol::object parentObj) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    NodeId *newParent = nullptr;
                    if (parentObj.is<SceneNodeHandle>())
                    {
                        SceneNodeHandle ph = parentObj.as<SceneNodeHandle>();
                        if (ph.IsValid(*s))
                            newParent = ph.nodeId;
                    }
                    s->ReparentNode(h.nodeId, newParent);
                });

                ut.set_function("get_mesh_refs", [](SceneNodeHandle &h) -> sol::as_table_t<std::vector<int>> {
                    std::vector<int> result;
                    Scene *s = GetScene();
                    if (s && h.IsValid(*s))
                    {
                        const auto &cache = s->GetNodeCache(h.nodeId);
                        result = cache.meshRefs->meshRefs;
                    }
                    return sol::as_table(std::move(result));
                });

                ut.set_function("set_mesh_ref", [](SceneNodeHandle &h, int meshIndex) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    if (meshIndex >= 0 && !s->IsValidMeshIndex(meshIndex)) return;
                    s->SetMeshRef(h.nodeId, meshIndex);
                });

                ut.set_function("add_mesh_ref", [](SceneNodeHandle &h, int meshIndex) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    if (!s->IsValidMeshIndex(meshIndex)) return;
                    s->AddMeshRef(h.nodeId, meshIndex);
                });

                ut.set_function("remove_mesh_ref", [](SceneNodeHandle &h, int meshIndex) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->RemoveMeshRef(h.nodeId, meshIndex);
                });

                ut.set_function("get_exposed", [](SceneNodeHandle &h, sol::this_state ts) -> std::optional<sol::table> {
                    lua_State *L = ts;
                    Scene *s = GetScene();
                    ScriptSystem *ss = s ? GetGlobalSystem<ScriptSystem>() : nullptr;
                    NodeScriptInstance *inst = (ss && h.IsValid(*s)) ? ss->FindNodeInstance(h.nodeId) : nullptr;

                    if (!inst || inst->exposedRef == LUA_NOREF)
                        return std::nullopt;

                    lua_rawgeti(L, LUA_REGISTRYINDEX, inst->exposedRef);
                    if (!lua_istable(L, -1))
                    {
                        lua_pop(L, 1);
                        return std::nullopt;
                    }

                    sol::table result(L, -1);
                    lua_pop(L, 1);
                    return result;
                }); });
        }
    } s_sceneNodeBindings;
} // namespace pe
