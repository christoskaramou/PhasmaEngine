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

                // Cheap render visibility (CullingCS cull-flag) — no instance/TLAS rebuild, unlike
                // set_enabled. Use for frequent per-frame show/hide of mesh nodes (pools, LOD).
                ut.set_function("set_visible", [](SceneNodeHandle &h, bool visible) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->SetNodeRenderVisible(h.nodeId, visible);
                });

                ut.set_function("is_visible", [](SceneNodeHandle &h) -> bool {
                    Scene *s = GetScene();
                    return s && h.IsValid(*s) && s->IsNodeRenderVisible(h.nodeId);
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

                ut.set_function("set_scene_settings", [](SceneNodeHandle &h, bool enabled) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s))
                        return;
                    if (enabled)
                    {
                        // Singleton, same as the hierarchy UI: ignore if another node already holds it,
                        // else GetSceneSettingsNode() (first match) would be order-dependent.
                        NodeId *existing = s->GetSceneSettingsNode();
                        if (existing && existing != h.nodeId)
                            return;
                        s->AddComponentFlag(h.nodeId, Component_SceneSettings);
                    }
                    else
                        s->RemoveComponentFlag(h.nodeId, Component_SceneSettings);
                    s->MarkDirty();
                });

                ut.set_function("set_trigger_zone", [](SceneNodeHandle &h, bool enabled) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    if (enabled)
                        s->AddComponentFlag(h.nodeId, Component_TriggerZone);
                    else
                        s->RemoveComponentFlag(h.nodeId, Component_TriggerZone);
                    s->MarkDirty();
                });

                // Set a Trigger Zone field by name.
                //  common:   "priority"/"blend"/"blend_distance"(float), "run_mode"(editor|player|both or int),
                //            "shape"(box|sphere or int)
                //  sections: "script_enabled"/"post_process_enabled"/"audio_enabled"/"physics_enabled"(bool)
                //  script:   "fire_for_camera"(bool), "on_enter"/"on_exit"(string)
                //  physics:  "physics_engine"(3d|2d), "physics_mode"(sensor|solid), "physics_body_type"(static|kinematic|dynamic),
                //            "physics_mass"/"physics_friction"/"physics_restitution"(float),
                //            "physics_filter"/"physics_script"/"physics_on_enter"/"physics_on_exit"(string),
                //            "physics_force_field"(bool), "physics_force"(vec3) — Sensor pushes bodies inside each frame
                //  spawn:    "spawn_enabled"(bool), "spawn_prefab"(string), "spawn_despawn_on_exit"(bool)
                //  stream:   "stream_enabled"(bool), "stream_target"(node name string)
                //  camera:   "camera_enabled"(bool), "camera_target"(camera node name), "camera_fov"(deg float, 0=no override)
                //  any PostProcessProfile field name writes into the zone's post-process profile (settings.* keys).
                ut.set_function("set_zone", [](SceneNodeHandle &h, const std::string &key, sol::object value) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    NodeTriggerZoneTag *z = s->GetTriggerZoneForNode(h.nodeId);
                    if (!z) return;
                    auto f = [&] { return static_cast<float>(value.as<double>()); };
                    if (key == "priority") { z->priority = f(); s->MarkDirty(); return; }
                    if (key == "blend") { z->blend = f(); s->MarkDirty(); return; }
                    if (key == "blend_distance") { z->blend_distance = f(); s->MarkDirty(); return; }
                    if (key == "run_mode") {
                        if (value.is<std::string>()) {
                            const std::string m = value.as<std::string>();
                            z->runMode = m == "editor" ? TriggerRunMode::Editor : m == "player" ? TriggerRunMode::Player : TriggerRunMode::Both;
                        } else z->runMode = static_cast<TriggerRunMode>(value.as<int>());
                        s->MarkDirty(); return;
                    }
                    if (key == "script_enabled") { z->scriptEnabled = value.as<bool>(); s->MarkDirty(); return; }
                    if (key == "post_process_enabled") { z->postProcessEnabled = value.as<bool>(); s->MarkDirty(); return; }
                    if (key == "audio_enabled") { z->audioEnabled = value.as<bool>(); s->MarkDirty(); return; }
                    if (key == "shape") {
                        if (value.is<std::string>()) z->shape = value.as<std::string>() == "sphere" ? ZoneShape::Sphere : ZoneShape::Box;
                        else z->shape = static_cast<ZoneShape>(value.as<int>());
                        s->MarkDirty(); return;
                    }
                    if (key == "physics_enabled") { z->physicsEnabled = value.as<bool>(); s->MarkDirty(); return; }
                    if (key == "physics_engine") {
                        if (value.is<std::string>()) z->physicsEngine = (value.as<std::string>() == "2d" || value.as<std::string>() == "physics2d") ? ZonePhysicsEngine::Physics2D : ZonePhysicsEngine::Physics3D;
                        else z->physicsEngine = static_cast<ZonePhysicsEngine>(value.as<int>());
                        s->MarkDirty(); return;
                    }
                    if (key == "physics_mode") {
                        if (value.is<std::string>()) z->physicsMode = value.as<std::string>() == "solid" ? ZonePhysicsMode::Solid : ZonePhysicsMode::Sensor;
                        else z->physicsMode = static_cast<ZonePhysicsMode>(value.as<int>());
                        s->MarkDirty(); return;
                    }
                    if (key == "physics_body_type") {
                        if (value.is<std::string>()) {
                            const std::string b = value.as<std::string>();
                            z->physicsBodyType = b == "dynamic" ? PhysicsBodyType::Dynamic : b == "kinematic" ? PhysicsBodyType::Kinematic : PhysicsBodyType::Static;
                        } else z->physicsBodyType = static_cast<PhysicsBodyType>(value.as<int>());
                        s->MarkDirty(); return;
                    }
                    if (key == "physics_mass") { z->physicsMass = f(); s->MarkDirty(); return; }
                    if (key == "physics_friction") { z->physicsFriction = f(); s->MarkDirty(); return; }
                    if (key == "physics_restitution") { z->physicsRestitution = f(); s->MarkDirty(); return; }
                    if (key == "physics_filter") { z->physicsFilterTag = value.as<std::string>(); s->MarkDirty(); return; }
                    if (key == "physics_script") { z->physicsScriptPath = value.as<std::string>(); s->MarkDirty(); return; }
                    if (key == "physics_on_enter") { z->physicsOnEnter = value.as<std::string>(); s->MarkDirty(); return; }
                    if (key == "physics_on_exit") { z->physicsOnExit = value.as<std::string>(); s->MarkDirty(); return; }
                    if (key == "physics_force_field") { z->physicsForceField = value.as<bool>(); s->MarkDirty(); return; }
                    if (key == "physics_force") { z->physicsForce = value.as<vec3>(); s->MarkDirty(); return; }
                    if (key == "spawn_enabled") { z->spawnEnabled = value.as<bool>(); s->MarkDirty(); return; }
                    if (key == "spawn_prefab") { z->spawnPrefabPath = value.as<std::string>(); s->MarkDirty(); return; }
                    if (key == "spawn_despawn_on_exit") { z->spawnDespawnOnExit = value.as<bool>(); s->MarkDirty(); return; }
                    if (key == "stream_enabled") { z->streamEnabled = value.as<bool>(); s->MarkDirty(); return; }
                    if (key == "stream_target") { z->streamTargetName = value.as<std::string>(); s->MarkDirty(); return; }
                    if (key == "camera_enabled") { z->cameraEnabled = value.as<bool>(); s->MarkDirty(); return; }
                    if (key == "camera_target") { z->cameraTargetName = value.as<std::string>(); s->MarkDirty(); return; }
                    if (key == "camera_fov") { z->cameraFovDeg = f(); s->MarkDirty(); return; }
                    if (key == "fire_for_camera") { z->fireForCamera = value.as<bool>(); s->MarkDirty(); return; }
                    if (key == "on_enter") { z->onEnter = value.as<std::string>(); s->MarkDirty(); return; }
                    if (key == "on_exit") { z->onExit = value.as<std::string>(); s->MarkDirty(); return; }
                    PostProcessProfile &p = z->postProcess;
                    static const std::unordered_map<std::string_view, bool PostProcessProfile::*> B = {
                        {"ssao", &PostProcessProfile::ssao}, {"fxaa", &PostProcessProfile::fxaa},
                        {"taa", &PostProcessProfile::taa}, {"cas_sharpening", &PostProcessProfile::cas_sharpening},
                        {"ssr", &PostProcessProfile::ssr}, {"tonemapping", &PostProcessProfile::tonemapping},
                        {"color_grading", &PostProcessProfile::color_grading}, {"dof", &PostProcessProfile::dof},
                        {"bloom", &PostProcessProfile::bloom}, {"motion_blur", &PostProcessProfile::motion_blur},
                        {"IBL", &PostProcessProfile::IBL}};
                    static const std::unordered_map<std::string_view, float PostProcessProfile::*> F = {
                        {"ssao_radius", &PostProcessProfile::ssao_radius}, {"ssao_bias", &PostProcessProfile::ssao_bias},
                        {"ssao_intensity", &PostProcessProfile::ssao_intensity}, {"ssao_power", &PostProcessProfile::ssao_power},
                        {"cas_sharpness", &PostProcessProfile::cas_sharpness},
                        {"color_grading_lift_r", &PostProcessProfile::color_grading_lift_r},
                        {"color_grading_lift_g", &PostProcessProfile::color_grading_lift_g},
                        {"color_grading_lift_b", &PostProcessProfile::color_grading_lift_b},
                        {"color_grading_gamma_r", &PostProcessProfile::color_grading_gamma_r},
                        {"color_grading_gamma_g", &PostProcessProfile::color_grading_gamma_g},
                        {"color_grading_gamma_b", &PostProcessProfile::color_grading_gamma_b},
                        {"color_grading_gain_r", &PostProcessProfile::color_grading_gain_r},
                        {"color_grading_gain_g", &PostProcessProfile::color_grading_gain_g},
                        {"color_grading_gain_b", &PostProcessProfile::color_grading_gain_b},
                        {"color_grading_saturation", &PostProcessProfile::color_grading_saturation},
                        {"color_grading_contrast", &PostProcessProfile::color_grading_contrast},
                        {"color_grading_intensity", &PostProcessProfile::color_grading_intensity},
                        {"dof_focus_scale", &PostProcessProfile::dof_focus_scale},
                        {"dof_blur_range", &PostProcessProfile::dof_blur_range},
                        {"bloom_strength", &PostProcessProfile::bloom_strength},
                        {"bloom_range", &PostProcessProfile::bloom_range},
                        {"motion_blur_strength", &PostProcessProfile::motion_blur_strength},
                        {"IBL_intensity", &PostProcessProfile::IBL_intensity}};
                    static const std::unordered_map<std::string_view, int PostProcessProfile::*> I = {
                        {"ssao_samples", &PostProcessProfile::ssao_samples},
                        {"motion_blur_samples", &PostProcessProfile::motion_blur_samples}};
                    if (auto it = B.find(key); it != B.end())
                        p.*(it->second) = value.as<bool>();
                    else if (auto it = F.find(key); it != F.end())
                        p.*(it->second) = static_cast<float>(value.as<double>());
                    else if (auto it = I.find(key); it != I.end())
                        p.*(it->second) = static_cast<int>(value.as<double>());
                    s->MarkDirty();
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
                    readColor("background_image_tint", ui->backgroundImageTint);

                    sol::object fontScale = opts["font_scale"];
                    if (fontScale.is<double>()) ui->fontScale = static_cast<float>(fontScale.as<double>());

                    // Text alignment: align_h/align_v accept a string ("left"/"center"/
                    // "right", "top"/"middle"/"bottom", "default") or a number 0..3.
                    // offset_x/offset_y nudge the text in pixels after alignment.
                    auto readAlign = [&](const char *key, uint8_t &dst, bool vertical) {
                        sol::object v = opts[key];
                        if (v.is<std::string>()) {
                            const std::string s = v.as<std::string>();
                            if (s == "default") dst = 0;
                            else if (!vertical) {
                                if (s == "left") dst = 1;
                                else if (s == "center" || s == "centre" || s == "middle") dst = 2;
                                else if (s == "right") dst = 3;
                            } else {
                                if (s == "top") dst = 1;
                                else if (s == "middle" || s == "center" || s == "centre") dst = 2;
                                else if (s == "bottom") dst = 3;
                            }
                        } else if (v.is<double>()) {
                            dst = static_cast<uint8_t>(v.as<double>());
                        }
                    };
                    readAlign("align_h", ui->textAlignH, false);
                    readAlign("align_v", ui->textAlignV, true);
                    sol::object offX = opts["offset_x"];
                    if (offX.is<double>()) ui->textOffset.x = static_cast<float>(offX.as<double>());
                    sol::object offY = opts["offset_y"];
                    if (offY.is<double>()) ui->textOffset.y = static_cast<float>(offY.as<double>());

                    sol::object visible = opts["visible"];
                    if (visible.is<bool>()) ui->visible = visible.as<bool>();
                    sol::object noInput = opts["no_input"];
                    if (noInput.is<bool>()) ui->noInput = noInput.as<bool>();
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

                // Inverse of get_world_position: place the node so its WORLD translation
                // lands on worldPos, keeping its own local rotation/scale. Under a parent
                // we map worldPos back through the parent's world matrix, so this is exact
                // for any parent transform (rotated/scaled included) — unlike set_position,
                // which sets the local translation directly.
                ut.set_function("set_world_position", [](SceneNodeHandle &h, const vec3 &worldPos) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    NodeId *parent = s->GetParent(h.nodeId);
                    vec3 localPos =
                        parent ? vec3(glm::inverse(ComputeNodeWorldMatrix(*s, parent)) * vec4(worldPos, 1.0f)) : worldPos;
                    // A singular parent world matrix (e.g. a zero/degenerate scale axis) makes the
                    // inverse non-finite; storing that would poison the node transform with NaN/inf,
                    // so leave the node untouched rather than corrupt it.
                    if (glm::any(glm::isnan(localPos)) || glm::any(glm::isinf(localPos)))
                        return;
                    mat4 local = s->GetLocalMatrix(h.nodeId);
                    local[3] = vec4(localPos, 1.0f);
                    s->SetLocalMatrix(h.nodeId, local);
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

                // mode (optional): "player" (default), "editor", or "both" — where the script's
                // init/update/destroy lifecycle runs.
                ut.set_function("set_script", [](SceneNodeHandle &h, const std::string &path, sol::optional<std::string> mode) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->SetNodeScript(h.nodeId, path);
                    if (mode)
                    {
                        const std::string &m = *mode;
                        s->SetNodeScriptRunMode(h.nodeId, m == "editor" ? ScriptRunMode::Editor
                                                          : m == "both" ? ScriptRunMode::Both
                                                                        : ScriptRunMode::Player);
                    }
                });

                ut.set_function("get_script", [](SceneNodeHandle &h) -> std::string {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return "";
                    return s->GetNodeScriptPath(h.nodeId);
                });

                ut.set_function("set_script_run_mode", [](SceneNodeHandle &h, const std::string &mode) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->SetNodeScriptRunMode(h.nodeId, mode == "editor" ? ScriptRunMode::Editor
                                                      : mode == "both" ? ScriptRunMode::Both
                                                                       : ScriptRunMode::Player);
                });

                ut.set_function("get_script_run_mode", [](SceneNodeHandle &h) -> std::string {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return "player";
                    switch (s->GetNodeScriptRunMode(h.nodeId))
                    {
                    case ScriptRunMode::Editor: return "editor";
                    case ScriptRunMode::Both: return "both";
                    default: return "player";
                    }
                });

                // Attach/update the scene's Voxel World component on this node (all keys optional):
                // enabled, streaming, anchor_follows_camera, load_radius, unload_margin, upload_budget,
                // ground_y, world_radius, lod_enabled, lod0_radius, save_dir, plus worldgen: noise_amplitude,
                // noise_feature_scale, noise_seed, caves, sea_level, heightmap, strata1_map, strata2_map,
                // features_map (pixel 1 = tree, 2 = rock),
                // blocks_per_pixel, surface_block, strata1_block, strata2_block, fill_block,
                // strata1_thickness, strata2_thickness, rebuild. VoxelSystem reconciles the
                // live world from it (same data the editor's Voxel World inspector edits).
                ut.set_function("set_voxel_world", [](SceneNodeHandle &h, sol::optional<sol::table> params) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->AddComponentFlag(h.nodeId, Component_VoxelWorld);
                    NodeVoxelWorldTag *v = s->GetVoxelWorldForNode(h.nodeId);
                    if (!v || !params) return;
                    sol::table p = *params;
                    if (p["enabled"].valid()) v->worldEnabled = p["enabled"];
                    if (p["streaming"].valid()) v->streaming = p["streaming"];
                    if (p["anchor_follows_camera"].valid()) v->anchorFollowsCamera = p["anchor_follows_camera"];
                    if (p["load_radius"].valid()) v->loadRadius = p["load_radius"];
                    if (p["unload_margin"].valid()) v->unloadMargin = p["unload_margin"];
                    if (p["upload_budget"].valid()) v->uploadBudget = p["upload_budget"];
                    if (p["ground_y"].valid()) v->groundY = p["ground_y"];
                    if (p["world_radius"].valid()) v->worldRadius = p["world_radius"];
                    if (p["lod_enabled"].valid()) v->lodEnabled = p["lod_enabled"];
                    if (p["lod0_radius"].valid()) v->lod0Radius = p["lod0_radius"];
                    if (p["save_dir"].valid()) v->saveDir = p["save_dir"].get<std::string>();
                    if (p["noise_amplitude"].valid()) v->noiseAmplitude = p["noise_amplitude"];
                    if (p["noise_feature_scale"].valid()) v->noiseFeatureScale = p["noise_feature_scale"];
                    if (p["noise_seed"].valid()) v->noiseSeed = p["noise_seed"];
                    if (p["caves"].valid()) v->caves = p["caves"];
                    if (p["sea_level"].valid()) v->seaLevel = p["sea_level"];
                    if (p["heightmap"].valid()) v->heightmapPath = p["heightmap"].get<std::string>();
                    if (p["strata1_map"].valid()) v->strata1Path = p["strata1_map"].get<std::string>();
                    if (p["strata2_map"].valid()) v->strata2Path = p["strata2_map"].get<std::string>();
                    if (p["features_map"].valid()) v->featuresPath = p["features_map"].get<std::string>();
                    if (p["blocks_per_pixel"].valid()) v->blocksPerPixel = p["blocks_per_pixel"];
                    if (p["height_min"].valid()) v->heightMin = p["height_min"];
                    if (p["height_max"].valid()) v->heightMax = p["height_max"];
                    if (p["ground_height"].valid()) v->groundHeight = p["ground_height"];
                    if (p["auto_rebuild"].valid()) v->autoRebuild = p["auto_rebuild"];
                    if (p["surface_block"].valid()) v->surfaceBlock = p["surface_block"];
                    if (p["surface_bands"].valid()) v->surfaceBands = p["surface_bands"];
                    if (p["strata1_block"].valid()) v->strata1Block = p["strata1_block"];
                    if (p["strata2_block"].valid()) v->strata2Block = p["strata2_block"];
                    if (p["fill_block"].valid()) v->fillBlock = p["fill_block"];
                    if (p["strata1_thickness"].valid()) v->strata1Thickness = p["strata1_thickness"];
                    if (p["strata2_thickness"].valid()) v->strata2Thickness = p["strata2_thickness"];
                    if (p["rebuild"].valid()) v->rebuildRequested = p["rebuild"];
                    s->MarkDirty();
                });

                ut.set_function("get_voxel_world", [&lua](SceneNodeHandle &h) -> sol::object {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return sol::lua_nil;
                    NodeVoxelWorldTag *v = s->GetVoxelWorldForNode(h.nodeId);
                    if (!v) return sol::lua_nil;
                    sol::table t = lua.create_table();
                    t["enabled"] = v->worldEnabled;
                    t["streaming"] = v->streaming;
                    t["anchor_follows_camera"] = v->anchorFollowsCamera;
                    t["load_radius"] = v->loadRadius;
                    t["unload_margin"] = v->unloadMargin;
                    t["upload_budget"] = v->uploadBudget;
                    t["ground_y"] = v->groundY;
                    t["world_radius"] = v->worldRadius;
                    t["lod_enabled"] = v->lodEnabled;
                    t["lod0_radius"] = v->lod0Radius;
                    t["save_dir"] = v->saveDir;
                    t["noise_amplitude"] = v->noiseAmplitude;
                    t["noise_feature_scale"] = v->noiseFeatureScale;
                    t["noise_seed"] = v->noiseSeed;
                    t["caves"] = v->caves;
                    t["sea_level"] = v->seaLevel;
                    t["heightmap"] = v->heightmapPath;
                    t["strata1_map"] = v->strata1Path;
                    t["strata2_map"] = v->strata2Path;
                    t["features_map"] = v->featuresPath;
                    t["blocks_per_pixel"] = v->blocksPerPixel;
                    t["height_min"] = v->heightMin;
                    t["height_max"] = v->heightMax;
                    t["ground_height"] = v->groundHeight;
                    t["auto_rebuild"] = v->autoRebuild;
                    t["surface_block"] = v->surfaceBlock;
                    t["surface_bands"] = v->surfaceBands;
                    t["strata1_block"] = v->strata1Block;
                    t["strata2_block"] = v->strata2Block;
                    t["fill_block"] = v->fillBlock;
                    t["strata1_thickness"] = v->strata1Thickness;
                    t["strata2_thickness"] = v->strata2Thickness;
                    return sol::make_object(lua, t);
                });

                // Terrain node config: size_x_meters, size_z_meters, ground_height, height_min,
                // height_max, sea_level_m, heightmap, caves_map, scatter_map, scatter_meshes,
                // noise_feature_scale, noise_seed, meters_per_pixel, physics, physics_friction,
                // physics_restitution, streaming, overhangs, collision_radius_m, auto_rebuild, rebuild.
                // TerrainSystem reconciles the live terrain from it (same data the Terrain inspector edits).
                ut.set_function("set_terrain", [](SceneNodeHandle &h, sol::optional<sol::table> params) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    s->AddComponentFlag(h.nodeId, Component_Terrain);
                    NodeTerrainTag *t = s->GetTerrainForNode(h.nodeId);
                    if (!t || !params) return;
                    sol::table p = *params;
                    if (p["enabled"].valid()) t->worldEnabled = p["enabled"];
                    if (p["size_x_meters"].valid()) t->sizeXMeters = p["size_x_meters"];
                    if (p["size_z_meters"].valid()) t->sizeZMeters = p["size_z_meters"];
                    if (p["ground_height"].valid()) t->groundHeight = p["ground_height"];
                    if (p["height_min"].valid()) t->heightMin = p["height_min"];
                    if (p["height_max"].valid()) t->heightMax = p["height_max"];
                    if (p["sea_level_m"].valid()) t->seaLevelM = p["sea_level_m"];
                    if (p["heightmap"].valid()) t->heightmapPath = p["heightmap"].get<std::string>();
                    if (p["caves_map"].valid()) t->cavesPath = p["caves_map"].get<std::string>();
                    if (p["scatter_map"].valid()) t->scatterPath = p["scatter_map"].get<std::string>();
                    if (p["scatter_meshes"].valid()) {
                        t->scatterMeshes.clear();
                        sol::table meshes = p["scatter_meshes"];
                        for (size_t i = 1; i <= meshes.size(); ++i)
                            t->scatterMeshes.push_back(meshes[i].get<std::string>());
                    }
                    if (p["splat_map"].valid()) t->splatPath = p["splat_map"].get<std::string>();
                    if (p["layers"].valid())
                    {
                        sol::table layers = p["layers"];
                        for (size_t i = 1; i <= layers.size() && i <= 4; ++i)
                            t->layerPaths[i - 1] = layers[i].get<std::string>();
                    }
                    if (p["material_maps"].valid())
                    {
                        sol::table mats = p["material_maps"];
                        for (size_t i = 1; i <= mats.size() && i <= 4; ++i)
                            t->materialPaths[i - 1] = mats[i].get<std::string>();
                    }
                    if (p["texture_scale"].valid()) t->textureScaleM = p["texture_scale"];
                    if (p["noise_feature_scale"].valid()) t->noiseFeatureScale = p["noise_feature_scale"];
                    if (p["noise_seed"].valid()) t->noiseSeed = p["noise_seed"];
                    if (p["meters_per_pixel"].valid()) t->metersPerPixel = p["meters_per_pixel"];
                    else if (p["blocks_per_pixel"].valid()) t->metersPerPixel = p["blocks_per_pixel"]; // pre-rename
                    if (p["physics"].valid()) t->physics = p["physics"];
                    if (p["physics_friction"].valid()) t->physicsFriction = p["physics_friction"];
                    if (p["physics_restitution"].valid()) t->physicsRestitution = p["physics_restitution"];
                    if (p["streaming"].valid()) t->streaming = p["streaming"];
                    if (p["overhangs"].valid()) t->overhangs = p["overhangs"];
                    if (p["collision_radius_m"].valid()) t->collisionRadiusM = p["collision_radius_m"];
                    if (p["auto_rebuild"].valid()) t->autoRebuild = p["auto_rebuild"];
                    if (p["rebuild"].valid()) t->rebuildRequested = p["rebuild"];
                    s->MarkDirty();
                });

                ut.set_function("get_terrain", [&lua](SceneNodeHandle &h) -> sol::object {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return sol::lua_nil;
                    NodeTerrainTag *t = s->GetTerrainForNode(h.nodeId);
                    if (!t) return sol::lua_nil;
                    sol::table r = lua.create_table();
                    r["enabled"] = t->worldEnabled;
                    r["size_x_meters"] = t->sizeXMeters;
                    r["size_z_meters"] = t->sizeZMeters;
                    r["ground_height"] = t->groundHeight;
                    r["height_min"] = t->heightMin;
                    r["height_max"] = t->heightMax;
                    r["sea_level_m"] = t->seaLevelM;
                    r["heightmap"] = t->heightmapPath;
                    r["caves_map"] = t->cavesPath;
                    r["scatter_map"] = t->scatterPath;
                    sol::table meshes = lua.create_table();
                    for (size_t i = 0; i < t->scatterMeshes.size(); ++i)
                        meshes[i + 1] = t->scatterMeshes[i];
                    r["scatter_meshes"] = meshes;
                    r["splat_map"] = t->splatPath;
                    sol::table layers = lua.create_table();
                    for (size_t i = 0; i < t->layerPaths.size(); ++i)
                        layers[i + 1] = t->layerPaths[i];
                    r["layers"] = layers;
                    sol::table mats = lua.create_table();
                    for (size_t i = 0; i < t->materialPaths.size(); ++i)
                        mats[i + 1] = t->materialPaths[i];
                    r["material_maps"] = mats;
                    r["texture_scale"] = t->textureScaleM;
                    r["noise_feature_scale"] = t->noiseFeatureScale;
                    r["noise_seed"] = t->noiseSeed;
                    r["meters_per_pixel"] = t->metersPerPixel;
                    r["physics"] = t->physics;
                    r["physics_friction"] = t->physicsFriction;
                    r["physics_restitution"] = t->physicsRestitution;
                    r["streaming"] = t->streaming;
                    r["overhangs"] = t->overhangs;
                    r["collision_radius_m"] = t->collisionRadiusM;
                    r["auto_rebuild"] = t->autoRebuild;
                    return sol::make_object(lua, r);
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

                // Per-mesh LOD shift: bias every mesh of this node toward coarser LODs (0 = automatic,
                // distance-only; 1+ forces that many levels coarser, clamped to the mesh's last LOD).
                ut.set_function("set_lod_shift", [](SceneNodeHandle &h, int shift) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    uint32_t v = static_cast<uint32_t>(std::clamp(shift, 0, static_cast<int>(Mesh::kMaxLods) - 1));
                    for (int mi : s->GetNodeCache(h.nodeId).meshRefs->meshRefs)
                        if (s->IsValidMeshIndex(mi))
                            s->GetMesh(mi).lodShift = v;
                    s->SetGeometryDirty();
                    s->MarkNodeDirty(h.nodeId);
                });

                ut.set_function("get_lod_shift", [](SceneNodeHandle &h) -> int {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return 0;
                    for (int mi : s->GetNodeCache(h.nodeId).meshRefs->meshRefs)
                        if (s->IsValidMeshIndex(mi))
                            return static_cast<int>(s->GetMesh(mi).lodShift);
                    return 0;
                });

                // Per-mesh LOD enable: false makes every mesh of this node ignore LOD (always full detail).
                ut.set_function("set_lod_enabled", [](SceneNodeHandle &h, bool enabled) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    for (int mi : s->GetNodeCache(h.nodeId).meshRefs->meshRefs)
                        if (s->IsValidMeshIndex(mi))
                            s->GetMesh(mi).lodEnabled = enabled;
                    s->SetGeometryDirty();
                    s->MarkNodeDirty(h.nodeId);
                });

                // Per-mesh LOD distance bias: multiplies camera distance for this node's meshes
                // (>1 = drop detail sooner, <1 = keep detail longer).
                ut.set_function("set_lod_bias", [](SceneNodeHandle &h, float bias) {
                    Scene *s = GetScene();
                    if (!s || !h.IsValid(*s)) return;
                    for (int mi : s->GetNodeCache(h.nodeId).meshRefs->meshRefs)
                        if (s->IsValidMeshIndex(mi))
                            s->GetMesh(mi).lodBias = bias;
                    s->SetGeometryDirty();
                    s->MarkNodeDirty(h.nodeId);
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
