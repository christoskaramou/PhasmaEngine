#ifdef PE_PHYSICS2D

#include "Physics2DSystem.h"
#include "Scene/Scene.h"
#include "Scene/SceneAccess.h"
#include "Scene/SceneNode.h"

#include <box2d/box2d.h>

namespace pe
{
    namespace
    {
        b2Vec2 ToB2(const vec2 &v)
        {
            return b2Vec2{v.x, v.y};
        }

        vec2 FromB2(const b2Vec2 &v)
        {
            return vec2(v.x, v.y);
        }

        b2BodyType ToB2(Physics2DBodyType type)
        {
            switch (type)
            {
            case Physics2DBodyType::Static:
                return b2_staticBody;
            case Physics2DBodyType::Kinematic:
                return b2_kinematicBody;
            case Physics2DBodyType::Dynamic:
                return b2_dynamicBody;
            default:
                return b2_staticBody;
            }
        }

        float ExtractZAngleRadians(const mat4 &matrix)
        {
            const vec3 xAxis = vec3(matrix[0]);
            if (dot(xAxis, xAxis) <= 0.000001f)
                return 0.0f;
            return std::atan2(xAxis.y, xAxis.x);
        }

        float WrappedAngleDelta(float a, float b)
        {
            constexpr float TWO_PI = 6.28318530717958647692f;
            constexpr float PI = 3.14159265358979323846f;
            float delta = a - b;
            while (delta > PI)
                delta -= TWO_PI;
            while (delta < -PI)
                delta += TWO_PI;
            return std::abs(delta);
        }

        mat4 Build2DNodeMatrix(float x, float y, float z, float angleRadians, const vec3 &scale)
        {
            mat4 t = translate(mat4(1.0f), vec3(x, y, z));
            mat4 r = rotate(mat4(1.0f), angleRadians, vec3(0.0f, 0.0f, 1.0f));
            mat4 s = glm::scale(mat4(1.0f), scale);
            return t * r * s;
        }

        vec3 ExtractScale(const mat4 &matrix)
        {
            return vec3(length(vec3(matrix[0])), length(vec3(matrix[1])), length(vec3(matrix[2])));
        }

        float PositiveOrDefault(float value, float fallback)
        {
            return value > 0.0f ? value : fallback;
        }
    } // namespace

    struct Physics2DSystem::Impl
    {
        b2WorldId worldId = b2_nullWorldId;
        bool hasWorld = false;
    };

    struct Physics2DSystem::BodyState
    {
        uint32_t publicId = 0;
        NodeId *nodeId = nullptr;
        uint32_t nodeRevision = 0;
        b2BodyId bodyId = b2_nullBodyId;
        b2ShapeId shapeId = b2_nullShapeId;
        Physics2DBodyDesc desc;
        bool syncNode = true;
    };

    Physics2DSystem::Physics2DSystem() = default;

    Physics2DSystem::~Physics2DSystem()
    {
        Destroy();
    }

    void Physics2DSystem::Init(CommandBuffer *)
    {
        PE_PROFILE_SCOPE("Physics2D Init");
        CreateWorld();
        SetEnabled(true);
    }

    void Physics2DSystem::Update()
    {
        PE_PROFILE_SCOPE("Physics2D System");

        m_contactEvents.clear();

        if (!m_impl || !m_impl->hasWorld || !b2World_IsValid(m_impl->worldId))
            return;

        Scene *scene = GetActiveScene();
        if (scene)
            PruneInvalidBodies(*scene);

        if (m_paused)
            return;

        const float rawDt = static_cast<float>(FrameTimer::Instance().GetDelta());
        const float dt = std::min(rawDt, FIXED_TIMESTEP * MAX_STEPS_PER_FRAME);
        m_accumulator += dt;

        int steps = 0;
        while (m_accumulator >= FIXED_TIMESTEP && steps < MAX_STEPS_PER_FRAME)
        {
            b2World_Step(m_impl->worldId, FIXED_TIMESTEP, SUBSTEP_COUNT);
            CollectEvents();
            m_accumulator -= FIXED_TIMESTEP;
            ++steps;
        }

        if (steps > 0 && scene)
            SyncTransformsToNodes(*scene);
    }

    void Physics2DSystem::Destroy()
    {
        PE_PROFILE_SCOPE("Physics2D Destroy");
        DestroyWorld();
        m_impl.reset();
    }

    uint32_t Physics2DSystem::AddBoxBody(Scene &scene, NodeId *node, Physics2DBodyType bodyType, float width, float height,
                                         const Physics2DBodyDesc &desc)
    {
        Physics2DBodyDesc bodyDesc = desc;
        bodyDesc.bodyType = bodyType;
        bodyDesc.shapeType = Physics2DShapeType::Box;
        bodyDesc.width = width;
        bodyDesc.height = height;
        return AddBody(scene, node, bodyDesc);
    }

    uint32_t Physics2DSystem::AddCircleBody(Scene &scene, NodeId *node, Physics2DBodyType bodyType, float radius,
                                            const Physics2DBodyDesc &desc)
    {
        Physics2DBodyDesc bodyDesc = desc;
        bodyDesc.bodyType = bodyType;
        bodyDesc.shapeType = Physics2DShapeType::Circle;
        bodyDesc.radius = radius;
        return AddBody(scene, node, bodyDesc);
    }

    uint32_t Physics2DSystem::AddCapsuleBody(Scene &scene, NodeId *node, Physics2DBodyType bodyType, float height, float radius,
                                             const Physics2DBodyDesc &desc)
    {
        Physics2DBodyDesc bodyDesc = desc;
        bodyDesc.bodyType = bodyType;
        bodyDesc.shapeType = Physics2DShapeType::Capsule;
        bodyDesc.capsuleHeight = height;
        bodyDesc.capsuleRadius = radius;
        return AddBody(scene, node, bodyDesc);
    }

    uint32_t Physics2DSystem::AddBody(Scene &scene, NodeId *node, const Physics2DBodyDesc &desc)
    {
        PE_PROFILE_SCOPE("Physics2D Add Body");

        if (!node || !scene.IsNodeAlive(node))
            return 0;

        if (!m_impl || !m_impl->hasWorld || !b2World_IsValid(m_impl->worldId))
            CreateWorld();

        if (!m_impl || !m_impl->hasWorld)
            return 0;

        RemoveBody(node);

        const mat4 &localMatrix = scene.GetLocalMatrix(node);
        const vec3 pos = vec3(localMatrix[3]);
        const float angle = ExtractZAngleRadians(localMatrix);

        auto state = std::make_unique<BodyState>();
        state->publicId = m_nextBodyId++;
        state->nodeId = node;
        state->nodeRevision = node->revision;
        state->desc = desc;
        state->desc.width = PositiveOrDefault(state->desc.width, 1.0f);
        state->desc.height = PositiveOrDefault(state->desc.height, 1.0f);
        state->desc.radius = PositiveOrDefault(state->desc.radius, 0.5f);
        state->desc.capsuleRadius = PositiveOrDefault(state->desc.capsuleRadius, 0.25f);
        state->desc.capsuleHeight = std::max(state->desc.capsuleHeight, state->desc.capsuleRadius * 2.0f);
        state->syncNode = state->desc.syncNode;

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = ToB2(state->desc.bodyType);
        bodyDef.position = b2Vec2{pos.x, pos.y};
        bodyDef.rotation = b2MakeRot(angle);
        bodyDef.linearDamping = state->desc.linearDamping;
        bodyDef.angularDamping = state->desc.angularDamping;
        bodyDef.gravityScale = state->desc.gravityScale;
        bodyDef.fixedRotation = state->desc.fixedRotation;
        bodyDef.isBullet = state->desc.bullet;
        bodyDef.enableSleep = state->desc.enableSleep;
        bodyDef.userData = state.get();

        state->bodyId = b2CreateBody(m_impl->worldId, &bodyDef);
        if (!b2Body_IsValid(state->bodyId))
            return 0;

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.userData = state.get();
        shapeDef.density = PositiveOrDefault(state->desc.density, 1.0f);
        shapeDef.material.friction = std::max(0.0f, state->desc.friction);
        shapeDef.material.restitution = std::max(0.0f, state->desc.restitution);
        shapeDef.filter.categoryBits = state->desc.categoryBits;
        shapeDef.filter.maskBits = state->desc.maskBits;
        shapeDef.filter.groupIndex = state->desc.groupIndex;
        shapeDef.isSensor = state->desc.isSensor;
        shapeDef.enableSensorEvents = true;
        shapeDef.enableContactEvents = !state->desc.isSensor;
        shapeDef.enableHitEvents = !state->desc.isSensor;

        if (state->desc.shapeType == Physics2DShapeType::Circle)
        {
            b2Circle circle{};
            circle.center = b2Vec2{0.0f, 0.0f};
            circle.radius = PositiveOrDefault(state->desc.radius, 0.01f);
            state->shapeId = b2CreateCircleShape(state->bodyId, &shapeDef, &circle);
        }
        else if (state->desc.shapeType == Physics2DShapeType::Capsule)
        {
            const float radius = PositiveOrDefault(state->desc.capsuleRadius, 0.01f);
            const float height = std::max(state->desc.capsuleHeight, radius * 2.0f);
            const float segment = std::max(0.0f, height - radius * 2.0f);
            b2Capsule capsule{};
            capsule.center1 = b2Vec2{0.0f, -segment * 0.5f};
            capsule.center2 = b2Vec2{0.0f, segment * 0.5f};
            capsule.radius = radius;
            state->shapeId = b2CreateCapsuleShape(state->bodyId, &shapeDef, &capsule);
        }
        else
        {
            const float halfWidth = PositiveOrDefault(state->desc.width, 0.01f) * 0.5f;
            const float halfHeight = PositiveOrDefault(state->desc.height, 0.01f) * 0.5f;
            b2Polygon box = b2MakeBox(halfWidth, halfHeight);
            state->shapeId = b2CreatePolygonShape(state->bodyId, &shapeDef, &box);
        }

        if (!b2Shape_IsValid(state->shapeId))
        {
            b2DestroyBody(state->bodyId);
            return 0;
        }

        const uint32_t id = state->publicId;
        m_nodeToBody[node] = id;
        m_bodies.emplace(id, std::move(state));
        scene.AddComponentFlag(node, Component_Physics2D);
        return id;
    }

    void Physics2DSystem::RemoveBody(uint32_t bodyId)
    {
        auto it = m_bodies.find(bodyId);
        if (it == m_bodies.end())
            return;

        BodyState &state = *it->second;
        NodeId *node = state.nodeId;
        const uint32_t nodeRevision = state.nodeRevision;
        if (b2Body_IsValid(state.bodyId))
            b2DestroyBody(state.bodyId);

        if (node)
            m_nodeToBody.erase(node);
        m_bodies.erase(it);

        if (node)
        {
            Scene *scene = GetActiveScene();
            if (scene && scene->IsNodeAlive(node) && node->revision == nodeRevision)
                scene->RemoveComponentFlag(node, Component_Physics2D);
        }
    }

    void Physics2DSystem::RemoveBody(NodeId *node)
    {
        auto it = m_nodeToBody.find(node);
        if (it == m_nodeToBody.end())
            return;
        RemoveBody(it->second);
    }

    Physics2DBodyDesc *Physics2DSystem::GetBodyDesc(NodeId *node)
    {
        BodyState *state = FindBody(GetBodyId(node));
        return state ? &state->desc : nullptr;
    }

    const Physics2DBodyDesc *Physics2DSystem::GetBodyDesc(const NodeId *node) const
    {
        const BodyState *state = FindBody(GetBodyId(node));
        return state ? &state->desc : nullptr;
    }

    bool Physics2DSystem::HasBody(uint32_t bodyId) const
    {
        return FindBody(bodyId) != nullptr;
    }

    bool Physics2DSystem::HasBody(const NodeId *node) const
    {
        return GetBodyId(node) != 0;
    }

    uint32_t Physics2DSystem::GetBodyId(const NodeId *node) const
    {
        auto it = m_nodeToBody.find(node);
        return it != m_nodeToBody.end() ? it->second : 0;
    }

    void Physics2DSystem::ClearWorld()
    {
        Scene *scene = GetActiveScene();
        std::vector<NodeId *> nodes;
        if (scene)
        {
            nodes.reserve(m_bodies.size());
            for (const auto &[id, state] : m_bodies)
            {
                (void)id;
                if (state->nodeId &&
                    scene->IsNodeAlive(state->nodeId) &&
                    state->nodeId->revision == state->nodeRevision)
                {
                    nodes.push_back(state->nodeId);
                }
            }
        }

        DestroyWorld();

        if (scene)
        {
            for (NodeId *node : nodes)
            {
                if (scene->IsNodeAlive(node))
                    scene->RemoveComponentFlag(node, Component_Physics2D);
            }
        }

        CreateWorld();
    }

    void Physics2DSystem::SetGravity(const vec2 &gravity)
    {
        m_gravity = gravity;
        if (m_impl && m_impl->hasWorld && b2World_IsValid(m_impl->worldId))
            b2World_SetGravity(m_impl->worldId, ToB2(m_gravity));
    }

    vec2 Physics2DSystem::GetGravity() const
    {
        if (m_impl && m_impl->hasWorld && b2World_IsValid(m_impl->worldId))
            return FromB2(b2World_GetGravity(m_impl->worldId));
        return m_gravity;
    }

    void Physics2DSystem::SetLinearVelocity(uint32_t bodyId, const vec2 &velocity)
    {
        if (BodyState *state = FindBody(bodyId))
            b2Body_SetLinearVelocity(state->bodyId, ToB2(velocity));
    }

    vec2 Physics2DSystem::GetLinearVelocity(uint32_t bodyId) const
    {
        if (const BodyState *state = FindBody(bodyId))
            return FromB2(b2Body_GetLinearVelocity(state->bodyId));
        return vec2(0.0f);
    }

    void Physics2DSystem::SetAngularVelocity(uint32_t bodyId, float velocity)
    {
        if (BodyState *state = FindBody(bodyId))
            b2Body_SetAngularVelocity(state->bodyId, velocity);
    }

    float Physics2DSystem::GetAngularVelocity(uint32_t bodyId) const
    {
        if (const BodyState *state = FindBody(bodyId))
            return b2Body_GetAngularVelocity(state->bodyId);
        return 0.0f;
    }

    void Physics2DSystem::ApplyForce(uint32_t bodyId, const vec2 &force)
    {
        if (BodyState *state = FindBody(bodyId))
            b2Body_ApplyForceToCenter(state->bodyId, ToB2(force), true);
    }

    void Physics2DSystem::ApplyImpulse(uint32_t bodyId, const vec2 &impulse)
    {
        if (BodyState *state = FindBody(bodyId))
            b2Body_ApplyLinearImpulseToCenter(state->bodyId, ToB2(impulse), true);
    }

    void Physics2DSystem::SetTransform(uint32_t bodyId, const vec2 &position, float angleRadians)
    {
        if (BodyState *state = FindBody(bodyId))
            b2Body_SetTransform(state->bodyId, ToB2(position), b2MakeRot(angleRadians));
    }

    bool Physics2DSystem::GetTransform(uint32_t bodyId, vec2 &position, float &angleRadians) const
    {
        const BodyState *state = FindBody(bodyId);
        if (!state)
            return false;

        const b2Transform transform = b2Body_GetTransform(state->bodyId);
        position = FromB2(transform.p);
        angleRadians = b2Rot_GetAngle(transform.q);
        return true;
    }

    void Physics2DSystem::SetNodeSync(uint32_t bodyId, bool enabled)
    {
        if (BodyState *state = FindBody(bodyId))
        {
            state->syncNode = enabled;
            state->desc.syncNode = enabled;
        }
    }

    Physics2DSystem::BodyState *Physics2DSystem::FindBody(uint32_t bodyId)
    {
        auto it = m_bodies.find(bodyId);
        if (it == m_bodies.end() || !b2Body_IsValid(it->second->bodyId))
            return nullptr;
        return it->second.get();
    }

    const Physics2DSystem::BodyState *Physics2DSystem::FindBody(uint32_t bodyId) const
    {
        auto it = m_bodies.find(bodyId);
        if (it == m_bodies.end() || !b2Body_IsValid(it->second->bodyId))
            return nullptr;
        return it->second.get();
    }

    void Physics2DSystem::CreateWorld()
    {
        if (!m_impl)
            m_impl = std::make_unique<Impl>();

        if (m_impl->hasWorld && b2World_IsValid(m_impl->worldId))
            return;

        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = ToB2(m_gravity);
        m_impl->worldId = b2CreateWorld(&worldDef);
        m_impl->hasWorld = b2World_IsValid(m_impl->worldId);
    }

    void Physics2DSystem::DestroyWorld()
    {
        if (m_impl && m_impl->hasWorld && b2World_IsValid(m_impl->worldId))
            b2DestroyWorld(m_impl->worldId);

        if (m_impl)
        {
            m_impl->worldId = b2_nullWorldId;
            m_impl->hasWorld = false;
        }

        m_bodies.clear();
        m_nodeToBody.clear();
        m_contactEvents.clear();
        m_accumulator = 0.0f;
    }

    void Physics2DSystem::PruneInvalidBodies(Scene &scene)
    {
        std::vector<uint32_t> deadBodies;
        for (const auto &[id, state] : m_bodies)
        {
            if (!state->nodeId ||
                !scene.IsNodeAlive(state->nodeId) ||
                state->nodeId->revision != state->nodeRevision)
            {
                deadBodies.push_back(id);
            }
        }

        for (uint32_t id : deadBodies)
            RemoveBody(id);
    }

    void Physics2DSystem::SyncTransformsToNodes(Scene &scene)
    {
        PE_PROFILE_SCOPE("Physics2D Sync Transforms");

        for (const auto &[id, state] : m_bodies)
        {
            (void)id;
            if (!state->syncNode ||
                !state->nodeId ||
                !scene.IsNodeAlive(state->nodeId) ||
                state->nodeId->revision != state->nodeRevision ||
                !b2Body_IsValid(state->bodyId))
            {
                continue;
            }

            const b2Transform transform = b2Body_GetTransform(state->bodyId);
            const mat4 &current = scene.GetLocalMatrix(state->nodeId);
            const vec3 scale = ExtractScale(current);
            const float z = current[3].z;
            const float angle = b2Rot_GetAngle(transform.q);
            const float currentAngle = ExtractZAngleRadians(current);
            const vec3 currentPosition = vec3(current[3]);
            if (std::abs(currentPosition.x - transform.p.x) <= 0.0001f &&
                std::abs(currentPosition.y - transform.p.y) <= 0.0001f &&
                WrappedAngleDelta(currentAngle, angle) <= 0.0001f)
            {
                continue;
            }

            scene.SetLocalMatrix(state->nodeId, Build2DNodeMatrix(transform.p.x, transform.p.y, z, angle, scale));
        }
    }

    void Physics2DSystem::CollectEvents()
    {
        auto stateFromShape = [](b2ShapeId shapeId) -> BodyState *
        {
            if (!b2Shape_IsValid(shapeId))
                return nullptr;
            return static_cast<BodyState *>(b2Shape_GetUserData(shapeId));
        };

        auto pushPair = [&](b2ShapeId shapeA, b2ShapeId shapeB, bool began, bool sensor)
        {
            BodyState *a = stateFromShape(shapeA);
            BodyState *b = stateFromShape(shapeB);
            if (!a || !b || a == b)
                return;

            Physics2DContactEvent event{};
            event.bodyA = a->publicId;
            event.bodyB = b->publicId;
            event.began = began;
            event.sensor = sensor;
            m_contactEvents.push_back(event);
        };

        const b2ContactEvents contacts = b2World_GetContactEvents(m_impl->worldId);
        for (int i = 0; i < contacts.beginCount; ++i)
        {
            const b2ContactBeginTouchEvent &event = contacts.beginEvents[i];
            pushPair(event.shapeIdA, event.shapeIdB, true, false);
        }
        for (int i = 0; i < contacts.endCount; ++i)
        {
            const b2ContactEndTouchEvent &event = contacts.endEvents[i];
            pushPair(event.shapeIdA, event.shapeIdB, false, false);
        }
        for (int i = 0; i < contacts.hitCount; ++i)
        {
            const b2ContactHitEvent &hit = contacts.hitEvents[i];
            BodyState *a = stateFromShape(hit.shapeIdA);
            BodyState *b = stateFromShape(hit.shapeIdB);
            if (!a || !b || a == b)
                continue;

            Physics2DContactEvent event{};
            event.bodyA = a->publicId;
            event.bodyB = b->publicId;
            event.hit = true;
            event.point = FromB2(hit.point);
            event.normal = FromB2(hit.normal);
            event.approachSpeed = hit.approachSpeed;
            m_contactEvents.push_back(event);
        }

        const b2SensorEvents sensors = b2World_GetSensorEvents(m_impl->worldId);
        for (int i = 0; i < sensors.beginCount; ++i)
        {
            const b2SensorBeginTouchEvent &event = sensors.beginEvents[i];
            pushPair(event.sensorShapeId, event.visitorShapeId, true, true);
        }
        for (int i = 0; i < sensors.endCount; ++i)
        {
            const b2SensorEndTouchEvent &event = sensors.endEvents[i];
            pushPair(event.sensorShapeId, event.visitorShapeId, false, true);
        }
    }
} // namespace pe

#endif // PE_PHYSICS2D
