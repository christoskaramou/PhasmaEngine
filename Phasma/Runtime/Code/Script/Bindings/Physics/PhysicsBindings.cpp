#ifdef PE_PHYSICS

#include "Physics/PhysicsTypes.h"
#include "Scene/SceneAccess.h"
#include "Scene/Scene.h"
#include "Scene/SceneNodeHandle.h"
#include "Script/ScriptSystem.h"
#include "Systems/PhysicsSystem.h"

namespace pe
{
    static void CallLuaTriggerCallback(const sol::function &callback, NodeId *trigger, NodeId *other, const char *eventName)
    {
        Scene *scene = GetActiveScene();
        if (!scene || !callback.valid() || !scene->IsNodeAlive(trigger) || !scene->IsNodeAlive(other))
            return;

        sol::protected_function protectedCallback(callback);
        sol::protected_function_result result = protectedCallback(scene->MakeHandle(other), scene->MakeHandle(trigger));
        if (!result.valid())
        {
            sol::error err = result;
            Log::Error(PeFormat("[Lua] physics.%s callback error: %s", eventName, err.what()));
        }
    }

    // A physics layer from Lua: an index (0..31) or a name in the scene's layer table. -1, with a warning, when
    // it is neither, so the caller changes nothing instead of falling back to Default.
    static int ResolvePhysicsLayer(const sol::object &value, const char *fn)
    {
        if (value.get_type() == sol::type::number)
        {
            const double index = value.as<double>();
            if (index >= 0.0 && index < SceneSettings::kPhysicsLayerCount && index == std::floor(index))
                return static_cast<int>(index);
        }
        else if (value.get_type() == sol::type::string)
        {
            const std::string name = value.as<std::string>();
            const auto &names = Settings::Get<SceneSettings>().physics_layer_names;
            for (uint32_t i = 0; i < SceneSettings::kPhysicsLayerCount; ++i)
                if (!name.empty() && names[i] == name)
                    return static_cast<int>(i);
        }
        PE_WARN("[Lua] physics.%s: not a physics layer index or name in this scene's layer table", fn);
        return -1;
    }

    static struct PhysicsBindings
    {
        PhysicsBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table physics = lua.create_named_table("physics");

                physics.set_function("add_body", [](SceneNodeHandle &h, const std::string &bodyType,
                                                     const std::string &shapeType, sol::optional<sol::table> params) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    Scene *scene = GetActiveScene();
                    if (!ps || !scene || !h.nodeId)
                        return;

                    PhysicsBodyDesc desc;
                    if (bodyType == "static")
                        desc.bodyType = PhysicsBodyType::Static;
                    else if (bodyType == "kinematic")
                        desc.bodyType = PhysicsBodyType::Kinematic;
                    else
                        desc.bodyType = PhysicsBodyType::Dynamic;

                    if (shapeType == "sphere")
                        desc.shapeType = PhysicsShapeType::Sphere;
                    else if (shapeType == "capsule")
                        desc.shapeType = PhysicsShapeType::Capsule;
                    else if (shapeType == "convex")
                        desc.shapeType = PhysicsShapeType::ConvexHull;
                    else
                        desc.shapeType = PhysicsShapeType::Box;

                    if (params)
                    {
                        sol::table p = *params;
                        if (p["mass"].valid())
                            desc.mass = p["mass"];
                        if (p["friction"].valid())
                            desc.friction = p["friction"];
                        if (p["restitution"].valid())
                            desc.restitution = p["restitution"];
                        if (p["is_trigger"].valid())
                            desc.isTrigger = p["is_trigger"];
                        if (p["layer"].valid())
                        {
                            const int layer = ResolvePhysicsLayer(p.get<sol::object>("layer"), "add_body");
                            if (layer >= 0)
                                desc.layer = static_cast<uint8_t>(layer);
                        }
                    }

                    ps->AddBody(*scene, h.nodeId, desc);
                });

                physics.set_function("set_layer", [](SceneNodeHandle &h, sol::object layer) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    Scene *scene = GetActiveScene();
                    if (!ps || !scene || !h.IsValid(*scene))
                        return;
                    const int index = ResolvePhysicsLayer(layer, "set_layer");
                    if (index >= 0)
                        ps->SetBodyLayer(h.nodeId, static_cast<uint8_t>(index));
                });

                physics.set_function("get_layer", [](SceneNodeHandle &h) -> sol::optional<int> {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    const PhysicsBodyDesc *desc = ps && h.nodeId ? ps->GetBodyDesc(h.nodeId) : nullptr;
                    if (!desc)
                        return sol::nullopt;
                    return static_cast<int>(desc->layer);
                });

                // Collision matrix entry, both directions; the scene's Physics Layers table edits the same bits.
                physics.set_function("set_layers_collide", [](sol::object a, sol::object b, bool collide) {
                    const int la = ResolvePhysicsLayer(a, "set_layers_collide");
                    const int lb = ResolvePhysicsLayer(b, "set_layers_collide");
                    if (la < 0 || lb < 0)
                        return;
                    auto &ignore = Settings::Get<SceneSettings>().physics_layer_ignore;
                    if (collide)
                    {
                        ignore[la] &= ~(1u << lb);
                        ignore[lb] &= ~(1u << la);
                    }
                    else
                    {
                        ignore[la] |= 1u << lb;
                        ignore[lb] |= 1u << la;
                    }
                });

                // physics.add_joint(node, "fixed" | "hinge" | "distance" | "slider", {connected = node or name (else
                // the world), anchor = vec3, axis = vec3, connected_anchor = vec3, limits = {min, max},
                // motor_speed = n, motor_max_force = n, break_force = n}). Replaces the node's joint.
                physics.set_function("add_joint", [](SceneNodeHandle &h, const std::string &type, sol::optional<sol::table> params) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    Scene *scene = GetActiveScene();
                    if (!ps || !scene || !h.IsValid(*scene))
                        return;
                    if (!ps->HasBody(h.nodeId))
                    {
                        PE_WARN("[Lua] physics.add_joint: '%s' has no physics body", scene->GetNodeName(h.nodeId).c_str());
                        return;
                    }
                    PhysicsJointDesc joint;
                    if (type == "fixed")
                        joint.type = PhysicsJointType::Fixed;
                    else if (type == "hinge")
                        joint.type = PhysicsJointType::Hinge;
                    else if (type == "distance")
                        joint.type = PhysicsJointType::Distance;
                    else if (type == "slider")
                        joint.type = PhysicsJointType::Slider;
                    else
                    {
                        PE_WARN("[Lua] physics.add_joint: unknown joint type '%s'", type.c_str());
                        return;
                    }
                    if (params)
                    {
                        sol::table p = *params;
                        const sol::object connected = p["connected"];
                        if (connected.is<SceneNodeHandle>())
                        {
                            const SceneNodeHandle &other = connected.as<SceneNodeHandle &>();
                            if (other.IsValid(*scene))
                                joint.connectedNode = scene->GetNodeName(other.nodeId);
                        }
                        else if (connected.get_type() == sol::type::string)
                            joint.connectedNode = connected.as<std::string>();
                        if (auto v = p.get<sol::optional<vec3>>("anchor"))
                            joint.anchor = *v;
                        if (auto v = p.get<sol::optional<vec3>>("axis"))
                            joint.axis = *v;
                        if (auto v = p.get<sol::optional<vec3>>("connected_anchor"))
                            joint.connectedAnchor = *v;
                        if (auto limits = p.get<sol::optional<sol::table>>("limits"))
                        {
                            joint.limitsEnabled = true;
                            joint.limitMin = (*limits)[1].get_or(joint.limitMin);
                            joint.limitMax = (*limits)[2].get_or(joint.limitMax);
                        }
                        if (auto v = p.get<sol::optional<float>>("motor_speed"))
                        {
                            joint.motorEnabled = true;
                            joint.motorSpeed = *v;
                        }
                        if (auto v = p.get<sol::optional<float>>("motor_max_force"))
                            joint.motorMaxForce = *v;
                        if (auto v = p.get<sol::optional<float>>("break_force"))
                            joint.breakForce = *v;
                    }
                    ps->SetJoint(*scene, h.nodeId, joint);
                });

                physics.set_function("remove_joint", [](SceneNodeHandle &h) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    if (ps && h.nodeId)
                        ps->RemoveJoint(h.nodeId);
                });

                // set_joint_motor(node, speed[, max_force]) drives a hinge (deg/s) or slider (m/s); speed = false stops it.
                physics.set_function("set_joint_motor", [](SceneNodeHandle &h, sol::object speed, sol::optional<float> maxForce) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    const PhysicsBodyDesc *desc = ps && h.nodeId ? ps->GetBodyDesc(h.nodeId) : nullptr;
                    if (!desc)
                        return;
                    const bool enabled = speed.get_type() == sol::type::number;
                    ps->SetJointMotor(h.nodeId, enabled, enabled ? speed.as<float>() : desc->joint.motorSpeed,
                                      maxForce.value_or(desc->joint.motorMaxForce));
                });

                // Hinge angle (deg) / slider position (m) from the pose at play start, distance length (m); nil without a live joint.
                physics.set_function("get_joint_value", [](SceneNodeHandle &h) -> sol::optional<float> {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    Scene *scene = GetActiveScene();
                    if (!ps || !scene || !h.IsValid(*scene))
                        return sol::nullopt;
                    const std::optional<float> value = ps->GetJointValue(*scene, h.nodeId);
                    if (!value)
                        return sol::nullopt;
                    return *value;
                });

                physics.set_function("on_joint_break", [](SceneNodeHandle &h, sol::function callback) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    Scene *scene = GetActiveScene();
                    if (!ps || !scene || !h.IsValid(*scene) || !callback.valid())
                        return;
                    ps->SetJointBreakCallback(h.nodeId, [callback = std::move(callback)](NodeId *node) {
                        Scene *active = GetActiveScene();
                        if (!active || !callback.valid() || !active->IsNodeAlive(node))
                            return;
                        sol::protected_function protectedCallback(callback);
                        sol::protected_function_result result = protectedCallback(active->MakeHandle(node));
                        if (!result.valid())
                        {
                            sol::error err = result;
                            Log::Error(PeFormat("[Lua] physics.on_joint_break callback error: %s", err.what()));
                        }
                    });
                });

                physics.set_function("remove_body", [](SceneNodeHandle &h) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    if (ps && h.nodeId)
                        ps->RemoveBody(h.nodeId);
                });

                physics.set_function("on_trigger_enter", [](SceneNodeHandle &h, sol::function callback) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    Scene *scene = GetActiveScene();
                    if (!ps || !scene || !h.IsValid(*scene) || !callback.valid())
                        return;

                    ps->SetTriggerEnterCallback(h.nodeId, [callback = std::move(callback)](NodeId *trigger, NodeId *other) {
                        CallLuaTriggerCallback(callback, trigger, other, "on_trigger_enter");
                    });
                });

                physics.set_function("on_trigger_exit", [](SceneNodeHandle &h, sol::function callback) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    Scene *scene = GetActiveScene();
                    if (!ps || !scene || !h.IsValid(*scene) || !callback.valid())
                        return;

                    ps->SetTriggerExitCallback(h.nodeId, [callback = std::move(callback)](NodeId *trigger, NodeId *other) {
                        CallLuaTriggerCallback(callback, trigger, other, "on_trigger_exit");
                    });
                });

                physics.set_function("clear_trigger_callbacks", [](SceneNodeHandle &h) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    Scene *scene = GetActiveScene();
                    if (ps && scene && h.IsValid(*scene))
                        ps->ClearTriggerCallbacks(h.nodeId);
                });

                physics.set_function("has_body", [](SceneNodeHandle &h) -> bool {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    return ps && h.nodeId && ps->HasBody(h.nodeId);
                });

                physics.set_function("set_velocity", [](SceneNodeHandle &h, float x, float y, float z) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    if (ps && h.nodeId)
                        ps->SetLinearVelocity(h.nodeId, vec3(x, y, z));
                });

                physics.set_function("get_velocity", [](SceneNodeHandle &h, sol::this_state ts) -> sol::object {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    if (!ps || !h.nodeId)
                        return sol::nil;
                    vec3 v = ps->GetLinearVelocity(h.nodeId);
                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    t["x"] = v.x;
                    t["y"] = v.y;
                    t["z"] = v.z;
                    return t;
                });

                physics.set_function("set_angular_velocity", [](SceneNodeHandle &h, float x, float y, float z) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    if (ps && h.nodeId)
                        ps->SetAngularVelocity(h.nodeId, vec3(x, y, z));
                });

                physics.set_function("apply_force", [](SceneNodeHandle &h, float x, float y, float z) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    if (ps && h.nodeId)
                        ps->ApplyForce(h.nodeId, vec3(x, y, z));
                });

                physics.set_function("apply_impulse", [](SceneNodeHandle &h, float x, float y, float z) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    if (ps && h.nodeId)
                        ps->ApplyImpulse(h.nodeId, vec3(x, y, z));
                });

                physics.set_function("apply_torque", [](SceneNodeHandle &h, float x, float y, float z) {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    if (ps && h.nodeId)
                        ps->ApplyTorque(h.nodeId, vec3(x, y, z));
                });

                physics.set_function("raycast", [](float ox, float oy, float oz,
                                                    float dx, float dy, float dz,
                                                    float maxDist, sol::object layers, sol::this_state ts) -> sol::object {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    if (!ps)
                        return sol::nil;

                    // Optional layers: one layer (index or name) or a table of them; omitted = every layer.
                    uint32_t mask = 0xFFFFFFFFu;
                    if (layers.valid() && layers.get_type() != sol::type::lua_nil)
                    {
                        mask = 0;
                        if (layers.get_type() == sol::type::table)
                        {
                            for (const auto &entry : layers.as<sol::table>())
                            {
                                const int layer = ResolvePhysicsLayer(entry.second, "raycast");
                                if (layer < 0)
                                    return sol::nil;
                                mask |= 1u << layer;
                            }
                        }
                        else
                        {
                            const int layer = ResolvePhysicsLayer(layers, "raycast");
                            if (layer < 0)
                                return sol::nil;
                            mask = 1u << layer;
                        }
                    }

                    RaycastResult result;
                    if (!ps->Raycast(vec3(ox, oy, oz), vec3(dx, dy, dz), maxDist, result, mask))
                        return sol::nil;

                    sol::state_view lua(ts);
                    sol::table t = lua.create_table();
                    t["fraction"] = result.fraction;
                    t["point_x"] = result.hitPoint.x;
                    t["point_y"] = result.hitPoint.y;
                    t["point_z"] = result.hitPoint.z;
                    t["normal_x"] = result.hitNormal.x;
                    t["normal_y"] = result.hitNormal.y;
                    t["normal_z"] = result.hitNormal.z;
                    return t;
                });

                physics.set_function("is_simulating", []() -> bool {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    return ps && ps->IsSimulating();
                }); });
        }
    } s_physicsBindings;
} // namespace pe

#endif // PE_PHYSICS
