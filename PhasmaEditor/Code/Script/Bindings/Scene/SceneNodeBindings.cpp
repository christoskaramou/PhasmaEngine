#include "Script/ScriptSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Scene/SceneNodeHandle.h"
#include "Systems/RendererSystem.h"

namespace pe
{
    static Scene *GetScene()
    {
        auto *r = GetGlobalSystem<RendererSystem>();
        return r ? &r->GetScene() : nullptr;
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

                ut.set_function("get_name", [](SceneNodeHandle &h) -> std::string {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return "";
                    return s->GetNodeName(h.nodeId);
                });

                ut.set_function("set_name", [](SceneNodeHandle &h, const std::string &name) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->SetNodeName(h.nodeId, name);
                });

                ut.set_function("get_position", [](SceneNodeHandle &h) -> vec3 {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return vec3(0.f);
                    return vec3(s->GetLocalMatrix(h.nodeId)[3]);
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
                    const mat4 &m = s->GetLocalMatrix(h.nodeId);
                    return vec3(glm::length(vec3(m[0])), glm::length(vec3(m[1])), glm::length(vec3(m[2])));
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

                ut.set_function("get_parent", [&lua](SceneNodeHandle &h) -> sol::object {
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

                ut.set_function("get_mesh_index", [](SceneNodeHandle &h) -> int {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return -1;
                    return s->GetMeshRef(h.nodeId);
                });

                ut.set_function("get_bounding_box", [&lua](SceneNodeHandle &h) -> sol::object {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return sol::make_object(lua, sol::nil);
                    const AABB &bb = s->GetWorldAABB(h.nodeId);
                    sol::table t = lua.create_table();
                    t["min"] = bb.min;
                    t["max"] = bb.max;
                    t["center"] = bb.GetCenter();
                    t["size"] = bb.GetSize();
                    return sol::make_object(lua, t);
                }); });
        }
    } s_sceneNodeBindings;
} // namespace pe
