#ifdef PE_PHYSICS2D

#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNodeHandle.h"
#include "Script/ScriptSystem.h"
#include "Systems/Physics2DSystem.h"

namespace pe
{
    namespace
    {
        Physics2DSystem *GetPhysics2D()
        {
            return HasGlobalSystem<Physics2DSystem>() ? GetGlobalSystem<Physics2DSystem>() : nullptr;
        }

        Physics2DBodyType ParseBodyType(std::string type)
        {
            std::transform(type.begin(), type.end(), type.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });

            if (type == "dynamic")
                return Physics2DBodyType::Dynamic;
            if (type == "kinematic")
                return Physics2DBodyType::Kinematic;
            return Physics2DBodyType::Static;
        }

        Physics2DShapeType ParseShapeType(std::string type)
        {
            std::transform(type.begin(), type.end(), type.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });

            if (type == "circle")
                return Physics2DShapeType::Circle;
            if (type == "capsule")
                return Physics2DShapeType::Capsule;
            return Physics2DShapeType::Box;
        }

        Physics2DBodyDesc ParseBodyDesc(sol::optional<sol::table> params)
        {
            Physics2DBodyDesc desc;
            if (!params)
                return desc;

            sol::table p = *params;
            if (p["body_type"].valid())
            {
                sol::object value = p["body_type"];
                if (value.is<std::string>())
                    desc.bodyType = ParseBodyType(value.as<std::string>());
                else if (value.is<int>())
                    desc.bodyType = static_cast<Physics2DBodyType>(std::clamp(value.as<int>(), 0, 2));
            }
            if (p["shape_type"].valid())
            {
                sol::object value = p["shape_type"];
                if (value.is<std::string>())
                    desc.shapeType = ParseShapeType(value.as<std::string>());
                else if (value.is<int>())
                    desc.shapeType = static_cast<Physics2DShapeType>(std::clamp(value.as<int>(), 0, 2));
            }
            if (p["width"].valid())
                desc.width = p["width"];
            if (p["height"].valid())
                desc.height = p["height"];
            if (p["radius"].valid())
                desc.radius = p["radius"];
            if (p["capsule_height"].valid())
                desc.capsuleHeight = p["capsule_height"];
            if (p["capsule_radius"].valid())
                desc.capsuleRadius = p["capsule_radius"];
            if (p["density"].valid())
                desc.density = p["density"];
            if (p["friction"].valid())
                desc.friction = p["friction"];
            if (p["restitution"].valid())
                desc.restitution = p["restitution"];
            if (p["linear_damping"].valid())
                desc.linearDamping = p["linear_damping"];
            if (p["angular_damping"].valid())
                desc.angularDamping = p["angular_damping"];
            if (p["gravity_scale"].valid())
                desc.gravityScale = p["gravity_scale"];
            if (p["fixed_rotation"].valid())
                desc.fixedRotation = p["fixed_rotation"];
            if (p["is_sensor"].valid())
                desc.isSensor = p["is_sensor"];
            if (p["sensor"].valid())
                desc.isSensor = p["sensor"];
            if (p["sync_node"].valid())
                desc.syncNode = p["sync_node"];
            if (p["bullet"].valid())
                desc.bullet = p["bullet"];
            if (p["enable_sleep"].valid())
                desc.enableSleep = p["enable_sleep"];
            if (p["category_bits"].valid())
                desc.categoryBits = p["category_bits"];
            if (p["mask_bits"].valid())
                desc.maskBits = p["mask_bits"];
            if (p["group_index"].valid())
                desc.groupIndex = p["group_index"];
            return desc;
        }

        uint32_t BodyIdFromNode(SceneNodeHandle &h)
        {
            Physics2DSystem *physics = GetPhysics2D();
            Scene *scene = GetActiveScene();
            if (!physics || !scene || !h.IsValid(*scene))
                return 0;
            return physics->GetBodyId(h.nodeId);
        }

        uint32_t ResolveBodyId(sol::object body)
        {
            if (body.is<uint32_t>())
                return body.as<uint32_t>();
            if (body.is<int>())
                return static_cast<uint32_t>(std::max(0, body.as<int>()));
            if (body.is<SceneNodeHandle>())
            {
                SceneNodeHandle h = body.as<SceneNodeHandle>();
                return BodyIdFromNode(h);
            }
            return 0;
        }

        sol::object MakeVec2Table(sol::state_view lua, const vec2 &v)
        {
            sol::table t = lua.create_table();
            t["x"] = v.x;
            t["y"] = v.y;
            return sol::make_object(lua, t);
        }
    } // namespace

    static struct Physics2DBindings
    {
        Physics2DBindings()
        {
            ScriptSystem::AddBindings([](sol::state &lua)
                                      {
                sol::table physics2d = lua.create_named_table("physics2d");

                physics2d.set_function("is_available", []() -> bool {
                    return GetPhysics2D() != nullptr;
                });

                physics2d.set_function("clear", []() {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->ClearWorld();
                });

                physics2d.set_function("set_paused", [](bool paused) {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->SetPaused(paused);
                });

                physics2d.set_function("is_paused", []() -> bool {
                    Physics2DSystem *physics = GetPhysics2D();
                    return physics ? physics->IsPaused() : true;
                });

                physics2d.set_function("set_gravity", [](float x, float y) {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->SetGravity(vec2(x, y));
                });

                physics2d.set_function("get_gravity", [](sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Physics2DSystem *physics = GetPhysics2D();
                    return MakeVec2Table(lua, physics ? physics->GetGravity() : vec2(0.0f));
                });

                physics2d.set_function("add_body", [](SceneNodeHandle &h, sol::optional<sol::table> params) -> uint32_t {
                    Physics2DSystem *physics = GetPhysics2D();
                    Scene *scene = GetActiveScene();
                    if (!physics || !scene || !h.IsValid(*scene))
                        return 0;
                    return physics->AddBody(*scene, h.nodeId, ParseBodyDesc(params));
                });

                physics2d.set_function("add_box", [](SceneNodeHandle &h, const std::string &bodyType, float width,
                                                      float height, sol::optional<sol::table> params) -> uint32_t {
                    Physics2DSystem *physics = GetPhysics2D();
                    Scene *scene = GetActiveScene();
                    if (!physics || !scene || !h.IsValid(*scene))
                        return 0;
                    return physics->AddBoxBody(*scene, h.nodeId, ParseBodyType(bodyType), width, height, ParseBodyDesc(params));
                });

                physics2d.set_function("add_circle", [](SceneNodeHandle &h, const std::string &bodyType, float radius,
                                                         sol::optional<sol::table> params) -> uint32_t {
                    Physics2DSystem *physics = GetPhysics2D();
                    Scene *scene = GetActiveScene();
                    if (!physics || !scene || !h.IsValid(*scene))
                        return 0;
                    return physics->AddCircleBody(*scene, h.nodeId, ParseBodyType(bodyType), radius, ParseBodyDesc(params));
                });

                physics2d.set_function("add_capsule", [](SceneNodeHandle &h, const std::string &bodyType, float height,
                                                          float radius, sol::optional<sol::table> params) -> uint32_t {
                    Physics2DSystem *physics = GetPhysics2D();
                    Scene *scene = GetActiveScene();
                    if (!physics || !scene || !h.IsValid(*scene))
                        return 0;
                    return physics->AddCapsuleBody(*scene, h.nodeId, ParseBodyType(bodyType), height, radius, ParseBodyDesc(params));
                });

                physics2d.set_function("remove_body", [](sol::object body) {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->RemoveBody(ResolveBodyId(body));
                });

                physics2d.set_function("has_body", [](sol::object body) -> bool {
                    Physics2DSystem *physics = GetPhysics2D();
                    return physics && physics->HasBody(ResolveBodyId(body));
                });

                physics2d.set_function("get_body_id", [](SceneNodeHandle &h) -> uint32_t {
                    return BodyIdFromNode(h);
                });

                physics2d.set_function("set_velocity", [](sol::object body, float x, float y) {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->SetLinearVelocity(ResolveBodyId(body), vec2(x, y));
                });

                physics2d.set_function("get_velocity", [](sol::object body, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Physics2DSystem *physics = GetPhysics2D();
                    return MakeVec2Table(lua, physics ? physics->GetLinearVelocity(ResolveBodyId(body)) : vec2(0.0f));
                });

                physics2d.set_function("set_angular_velocity", [](sol::object body, float velocity) {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->SetAngularVelocity(ResolveBodyId(body), velocity);
                });

                physics2d.set_function("get_angular_velocity", [](sol::object body) -> float {
                    Physics2DSystem *physics = GetPhysics2D();
                    return physics ? physics->GetAngularVelocity(ResolveBodyId(body)) : 0.0f;
                });

                physics2d.set_function("apply_force", [](sol::object body, float x, float y) {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->ApplyForce(ResolveBodyId(body), vec2(x, y));
                });

                physics2d.set_function("apply_impulse", [](sol::object body, float x, float y) {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->ApplyImpulse(ResolveBodyId(body), vec2(x, y));
                });

                physics2d.set_function("set_transform", [](sol::object body, float x, float y, float angleRadians) {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->SetTransform(ResolveBodyId(body), vec2(x, y), angleRadians);
                });

                physics2d.set_function("get_transform", [](sol::object body, sol::this_state ts) -> sol::object {
                    sol::state_view lua(ts);
                    Physics2DSystem *physics = GetPhysics2D();
                    if (!physics)
                        return sol::nil;

                    vec2 position(0.0f);
                    float angle = 0.0f;
                    if (!physics->GetTransform(ResolveBodyId(body), position, angle))
                        return sol::nil;

                    sol::table t = lua.create_table();
                    t["x"] = position.x;
                    t["y"] = position.y;
                    t["angle"] = angle;
                    return sol::make_object(lua, t);
                });

                physics2d.set_function("set_node_sync", [](sol::object body, bool enabled) {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->SetNodeSync(ResolveBodyId(body), enabled);
                });

                physics2d.set_function("get_contacts", [](sol::this_state ts) -> sol::table {
                    sol::state_view lua(ts);
                    sol::table contacts = lua.create_table();
                    Physics2DSystem *physics = GetPhysics2D();
                    if (!physics)
                        return contacts;

                    int index = 1;
                    for (const Physics2DContactEvent &event : physics->GetContactEvents())
                    {
                        sol::table item = lua.create_table();
                        item["a"] = event.bodyA;
                        item["b"] = event.bodyB;
                        item["began"] = event.began;
                        item["ended"] = !event.began && !event.hit;
                        item["sensor"] = event.sensor;
                        item["hit"] = event.hit;
                        item["point_x"] = event.point.x;
                        item["point_y"] = event.point.y;
                        item["normal_x"] = event.normal.x;
                        item["normal_y"] = event.normal.y;
                        item["approach_speed"] = event.approachSpeed;
                        contacts[index++] = item;
                    }
                    return contacts;
                });

                physics2d.set_function("clear_contacts", []() {
                    if (Physics2DSystem *physics = GetPhysics2D())
                        physics->ClearContactEvents();
                }); });
        }
    } s_physics2DBindings;
} // namespace pe

#endif // PE_PHYSICS2D
