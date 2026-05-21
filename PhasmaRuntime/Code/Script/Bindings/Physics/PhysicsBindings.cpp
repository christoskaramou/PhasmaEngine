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
                    }

                    ps->AddBody(*scene, h.nodeId, desc);
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
                                                    float maxDist, sol::this_state ts) -> sol::object {
                    auto *ps = GetGlobalSystem<PhysicsSystem>();
                    if (!ps)
                        return sol::nil;

                    RaycastResult result;
                    if (!ps->Raycast(vec3(ox, oy, oz), vec3(dx, dy, dz), maxDist, result))
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
