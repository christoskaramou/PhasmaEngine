#ifdef PE_PHYSICS

#include "PhysicsSystem.h"
#include "Scene/SceneAccess.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Camera/Camera.h"

// Undef X11 macros that conflict with Jolt
#ifdef None
#undef None
#endif
#ifdef Bool
#undef Bool
#endif
#ifdef Status
#undef Status
#endif
#ifdef True
#undef True
#endif
#ifdef False
#undef False
#endif
#ifdef Convex
#undef Convex
#endif

// Jolt includes
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

JPH_SUPPRESS_WARNINGS

namespace pe
{
    namespace
    {
        // Scenes can queue up large batches of dynamic primitives before entering play mode.
        // The original limits were too small and Jolt started reporting update errors once
        // contact pairs and constraints grew during settling.
        constexpr uint32_t kMaxBodies = 16384;
        constexpr uint32_t kMaxBodyPairs = 65536;
        constexpr uint32_t kMaxContactConstraints = 65536;
        constexpr size_t kTempAllocatorBytes = 64 * 1024 * 1024;

        constexpr size_t kInvalidBodyIndex = static_cast<size_t>(-1);

        bool MatchesNode(const PhysicsNodeState &state, const NodeId *node)
        {
            return node && state.nodeId == node && state.nodeRevision == node->revision;
        }

        bool MatricesNearEqual(const mat4 &a, const mat4 &b, float epsilon = 1e-5f)
        {
            const float *pa = value_ptr(a);
            const float *pb = value_ptr(b);
            for (int i = 0; i < 16; ++i)
            {
                if (std::abs(pa[i] - pb[i]) > epsilon)
                    return false;
            }
            return true;
        }

        bool IsLivePhysicsState(const PhysicsNodeState &state, const Scene &scene)
        {
            return state.nodeId &&
                   scene.IsNodeAlive(state.nodeId) &&
                   state.nodeId->revision == state.nodeRevision &&
                   (scene.GetComponentFlags(state.nodeId) & Component_Physics) != 0;
        }
    } // namespace

    // --- Jolt layer definitions ---

    // A Jolt object layer is the user layer (SceneSettings::physics_layer_names index) times two plus a moving
    // bit, so the broadphase keeps its static / moving split and the pair filter reads the layer table.
    namespace Layers
    {
        static constexpr JPH::ObjectLayer ToObjectLayer(uint32_t userLayer, bool moving)
        {
            return static_cast<JPH::ObjectLayer>(std::min(userLayer, SceneSettings::kPhysicsLayerCount - 1) * 2u +
                                                 (moving ? 1u : 0u));
        }
        static constexpr uint32_t UserLayer(JPH::ObjectLayer layer)
        {
            return layer >> 1;
        }
        static constexpr bool IsMoving(JPH::ObjectLayer layer)
        {
            return (layer & 1u) != 0;
        }
        static constexpr JPH::ObjectLayer NON_MOVING = ToObjectLayer(0, false);
    } // namespace Layers

    namespace BroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr uint32_t NUM_LAYERS = 2;
    } // namespace BroadPhaseLayers

    class BPLayerInterface final : public JPH::BroadPhaseLayerInterface
    {
    public:
        JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }

        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
        {
            return Layers::IsMoving(layer) ? BroadPhaseLayers::MOVING : BroadPhaseLayers::NON_MOVING;
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
        {
            switch ((JPH::BroadPhaseLayer::Type)layer)
            {
            case 0:
                return "NON_MOVING";
            case 1:
                return "MOVING";
            default:
                return "UNKNOWN";
            }
        }
#endif
    };

    class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer obj, JPH::BroadPhaseLayer bp) const override
        {
            if (!Layers::IsMoving(obj))
                return bp == BroadPhaseLayers::MOVING;
            return true;
        }
    };

    class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
    {
    public:
        bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
        {
            if (!Layers::IsMoving(a) && !Layers::IsMoving(b))
                return false;
            const uint32_t la = Layers::UserLayer(a), lb = Layers::UserLayer(b);
            return (((ignore[la] >> lb) | (ignore[lb] >> la)) & 1u) == 0;
        }

        // Copy of SceneSettings::physics_layer_ignore, refreshed before each step (Jolt reads it on its workers).
        std::array<uint32_t, SceneSettings::kPhysicsLayerCount> ignore{};
    };

    // Raycast filter: only bodies whose user layer is in the mask.
    class LayerMaskFilter final : public JPH::ObjectLayerFilter
    {
    public:
        explicit LayerMaskFilter(uint32_t mask) : m_mask(mask) {}
        bool ShouldCollide(JPH::ObjectLayer layer) const override { return ((m_mask >> Layers::UserLayer(layer)) & 1u) != 0; }

    private:
        uint32_t m_mask;
    };

    // Static instances for Jolt callbacks
    static BPLayerInterface s_bpLayerInterface;
    static ObjectVsBroadPhaseFilter s_objVsBpFilter;
    static ObjectLayerPairFilter s_objLayerPairFilter;

    class TriggerContactListener final : public JPH::ContactListener
    {
    public:
        explicit TriggerContactListener(PhysicsSystem &owner) : m_owner(owner) {}

        void OnContactAdded(const JPH::Body &body1, const JPH::Body &body2,
                            const JPH::ContactManifold &, JPH::ContactSettings &) override
        {
            if (!body1.IsSensor() && !body2.IsSensor())
                return;

            m_owner.QueueTriggerContact(body1.GetID().GetIndexAndSequenceNumber(),
                                        body2.GetID().GetIndexAndSequenceNumber(), true);
        }

        void OnContactRemoved(const JPH::SubShapeIDPair &subShapePair) override
        {
            m_owner.QueueTriggerContact(subShapePair.GetBody1ID().GetIndexAndSequenceNumber(),
                                        subShapePair.GetBody2ID().GetIndexAndSequenceNumber(), false);
        }

    private:
        PhysicsSystem &m_owner;
    };

    // --- PhysicsSystem implementation ---

    PhysicsSystem::~PhysicsSystem()
    {
        Destroy();
    }

    void PhysicsSystem::Init(CommandBuffer *)
    {
        PE_PROFILE_SCOPE("Physics Init");

        JPH::RegisterDefaultAllocator();

        if (!JPH::Factory::sInstance)
            JPH::Factory::sInstance = new JPH::Factory();

        JPH::RegisterTypes();

        m_tempAllocator = new JPH::TempAllocatorImpl(kTempAllocatorBytes);
        m_jobSystem = new JPH::JobSystemThreadPool(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
            static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1)));

        constexpr uint32_t numBodyMutexes = 0; // auto

        m_joltSystem = new JPH::PhysicsSystem();
        m_joltSystem->Init(kMaxBodies, numBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
                           s_bpLayerInterface, s_objVsBpFilter, s_objLayerPairFilter);
        m_contactListener = new TriggerContactListener(*this);
        m_joltSystem->SetContactListener(m_contactListener);

        m_joltSystem->SetGravity(JPH::Vec3(0.0f, -9.81f, 0.0f));

        // Reduce solver iterations for large-body-count scenes — trades a small
        // amount of contact precision for ~30% cheaper physics steps.
        JPH::PhysicsSettings ps;
        ps.mNumVelocitySteps = 4; // default 10
        ps.mNumPositionSteps = 1; // default 2
        m_joltSystem->SetPhysicsSettings(ps);

        SetEnabled(true);
    }

    void PhysicsSystem::Update()
    {
        PE_PROFILE_SCOPE("Physics System");

        if (!m_simulating || m_paused)
            return;

        Scene *scene = GetActiveScene();
        if (!scene)
            return;

        const SceneSettings &settings = Settings::Get<SceneSettings>();
        s_objLayerPairFilter.ignore = settings.physics_layer_ignore;
        const float step = 1.0f / static_cast<float>(std::clamp(settings.physics_rate, 10u, 240u));
        const int maxSteps = std::max(1, static_cast<int>(MAX_CATCHUP_SECONDS / step + 0.5f));
        const float rawDt = static_cast<float>(FrameTimer::Instance().GetDelta());
        {
            PE_PROFILE_SCOPE("Physics Frame Budget");
            m_accumulator += std::min(rawDt, step * maxSteps);
        }

        int steps = 0;
        {
            PE_PROFILE_SCOPE("Physics Step Loop");
            while (m_accumulator >= step && steps < maxSteps)
            {
                PE_PROFILE_SCOPE("Physics Step");
                m_joltSystem->Update(step, 1, m_tempAllocator, m_jobSystem);
                m_accumulator -= step;
                steps++;
            }
        }

        if (steps > 0 && !m_joints.empty())
            CheckJointBreaks(step);

        if (steps > 0)
        {
            PE_PROFILE_SCOPE("Physics Sync Transforms");
            SyncTransformsFromJolt(*scene);
            DrainTriggerContacts(*scene);
        }
    }

    void PhysicsSystem::Destroy()
    {
        PE_PROFILE_SCOPE("Physics Destroy");

        if (!m_joltSystem)
            return; // Already destroyed

        StopSimulation();
        ClearAllBodies();
        m_staticMeshBodies.clear(); // die with the Jolt world; owners re-add after the next Init
        m_joltSystem->SetContactListener(nullptr);
        delete m_contactListener;
        m_contactListener = nullptr;
        delete m_joltSystem;
        m_joltSystem = nullptr;
        delete m_jobSystem;
        m_jobSystem = nullptr;
        delete m_tempAllocator;
        m_tempAllocator = nullptr;

        JPH::UnregisterTypes();
        if (JPH::Factory::sInstance)
        {
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }

    void PhysicsSystem::AddBody(Scene &scene, NodeId *node, const PhysicsBodyDesc &desc)
    {
        PE_PROFILE_SCOPE("Physics Add Body");

        auto existingIt = m_nodeToIndex.find(node);
        if (existingIt != m_nodeToIndex.end())
        {
            const size_t existingIdx = existingIt->second;
            if (existingIdx < m_bodies.size() && MatchesNode(m_bodies[existingIdx], node))
                return;

            // Stale mapping for a recycled node pointer; rebuild from live scene before adding.
            PruneInvalidBodies(scene);
            if (m_nodeToIndex.count(node))
                return;
        }

        if (!node)
            return;

        scene.AddComponentFlag(node, Component_Physics);

        PhysicsNodeState state;
        state.nodeId = node;
        state.nodeRevision = node->revision;
        state.desc = desc;

        // Auto-fit shape from union of all mesh AABBs
        if (desc.autoFitShape)
        {
            PE_PROFILE_SCOPE("Physics Auto Fit Shape");
            const auto &refs = scene.GetNodeCache(node).meshRefs->meshRefs;
            AABB combined;
            bool init = false;
            for (int meshRef : refs)
            {
                if (meshRef < 0)
                    continue;
                const AABB &bb = scene.GetMesh(meshRef).boundingBox;
                if (!init)
                {
                    combined = bb;
                    init = true;
                }
                else
                {
                    combined.min = min(combined.min, bb.min);
                    combined.max = max(combined.max, bb.max);
                }
            }
            if (init)
            {
                vec3 size = combined.GetSize();
                state.desc.boxHalfExtents = size * 0.5f;
                state.desc.sphereRadius = std::max({size.x, size.y, size.z}) * 0.5f;
                state.desc.capsuleRadius = std::max(size.x, size.z) * 0.5f;
                state.desc.capsuleHalfHeight = std::max(size.y * 0.5f - state.desc.capsuleRadius, 0.06f);
            }
        }

        // Cache the node's authored scale once so SyncTransformsFromJolt avoids
        // three glm::length calls per active body per frame.
        {
            const mat4 &local = scene.GetLocalMatrix(node);
            state.authoredScale = vec3(
                glm::length(vec3(local[0])),
                glm::length(vec3(local[1])),
                glm::length(vec3(local[2])));
        }

        size_t idx = m_bodies.size();
        m_bodies.push_back(std::move(state));
        m_nodeToIndex[node] = idx;

        if (m_simulating)
        {
            CreateJoltBody(m_bodies.back(), scene);
            CreateJoint(m_bodies.back(), scene);
        }
    }

    void PhysicsSystem::RemoveBody(NodeId *node)
    {
        PE_PROFILE_SCOPE("Physics Remove Body");

        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end())
            return;

        if (it->second >= m_bodies.size() || !MatchesNode(m_bodies[it->second], node))
        {
            m_nodeToIndex.erase(it);
            return;
        }

        if (Scene *scene = GetActiveScene())
            scene->RemoveComponentFlag(node, Component_Physics);

        size_t idx = it->second;

        if (m_bodies[idx].inWorld)
            DestroyJoltBody(m_bodies[idx], true);
        else if (m_bodies[idx].cachedShape)
        {
            m_bodies[idx].cachedShape->Release();
            m_bodies[idx].cachedShape = nullptr;
        }

        // Swap-and-pop
        if (idx < m_bodies.size() - 1)
        {
            m_bodies[idx] = std::move(m_bodies.back());
            m_nodeToIndex[m_bodies[idx].nodeId] = idx;
            if (m_bodies[idx].inWorld)
                m_bodyIdToIndex[m_bodies[idx].joltBodyIdRaw] = idx;
        }
        m_bodies.pop_back();
        m_nodeToIndex.erase(it);
    }

    PhysicsBodyDesc *PhysicsSystem::GetBodyDesc(NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || it->second >= m_bodies.size() || !MatchesNode(m_bodies[it->second], node))
            return nullptr;
        return &m_bodies[it->second].desc;
    }

    const PhysicsBodyDesc *PhysicsSystem::GetBodyDesc(const NodeId *node) const
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || it->second >= m_bodies.size() || !MatchesNode(m_bodies[it->second], node))
            return nullptr;
        return &m_bodies[it->second].desc;
    }

    bool PhysicsSystem::HasBody(const NodeId *node) const
    {
        auto it = m_nodeToIndex.find(node);
        return it != m_nodeToIndex.end() && it->second < m_bodies.size() && MatchesNode(m_bodies[it->second], node);
    }

    void PhysicsSystem::ClearAllBodies()
    {
        PE_PROFILE_SCOPE("Physics Clear Bodies");

        for (auto &state : m_bodies)
        {
            if (state.inWorld)
            {
                DestroyJoltBody(state, true);
            }
            else if (state.cachedShape)
            {
                state.cachedShape->Release();
                state.cachedShape = nullptr;
            }
        }
        m_bodies.clear();
        m_nodeToIndex.clear();
        m_bodyIdToIndex.clear();
        ClearTriggerContactState();
    }

    void PhysicsSystem::InvalidateShapeCache(NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || it->second >= m_bodies.size() || !MatchesNode(m_bodies[it->second], node))
            return;
        PhysicsNodeState &state = m_bodies[it->second];
        if (!state.inWorld && state.cachedShape)
        {
            state.cachedShape->Release();
            state.cachedShape = nullptr;
        }
    }

    void PhysicsSystem::NotifyScaleChanged(Scene &scene, NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || it->second >= m_bodies.size() || !MatchesNode(m_bodies[it->second], node))
            return;
        PhysicsNodeState &state = m_bodies[it->second];
        const mat4 &local = scene.GetLocalMatrix(node);
        state.authoredScale = vec3(
            glm::length(vec3(local[0])),
            glm::length(vec3(local[1])),
            glm::length(vec3(local[2])));
        // Shape dimensions are baked with world scale — must rebuild on next play.
        if (!state.inWorld && state.cachedShape)
        {
            state.cachedShape->Release();
            state.cachedShape = nullptr;
        }
    }

    // --- Runtime velocity/force API ---

    void PhysicsSystem::SetLinearVelocity(NodeId *node, const vec3 &vel)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || !m_bodies[it->second].inWorld)
            return;
        JPH::BodyID bodyId(m_bodies[it->second].joltBodyIdRaw);
        m_joltSystem->GetBodyInterface().SetLinearVelocity(bodyId, JPH::Vec3(vel.x, vel.y, vel.z));
    }

    void PhysicsSystem::SetBodyLayer(NodeId *node, uint8_t layer)
    {
        PhysicsBodyDesc *desc = GetBodyDesc(node);
        if (!desc)
            return;
        desc->layer = static_cast<uint8_t>(std::min<uint32_t>(layer, SceneSettings::kPhysicsLayerCount - 1));
        const PhysicsNodeState &state = m_bodies[m_nodeToIndex.at(node)];
        if (state.inWorld && m_joltSystem)
            m_joltSystem->GetBodyInterface().SetObjectLayer(
                JPH::BodyID(state.joltBodyIdRaw),
                Layers::ToObjectLayer(desc->layer, desc->bodyType != PhysicsBodyType::Static));
    }

    void PhysicsSystem::SetBodyMaterial(NodeId *node, float friction, float restitution)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || it->second >= m_bodies.size())
            return;
        PhysicsNodeState &st = m_bodies[it->second];
        st.desc.friction = friction; // keep the desc in sync so the next cook preserves it
        st.desc.restitution = restitution;
        if (st.inWorld && m_joltSystem)
        {
            JPH::BodyID bodyId(st.joltBodyIdRaw);
            JPH::BodyInterface &bi = m_joltSystem->GetBodyInterface();
            bi.SetFriction(bodyId, friction);
            bi.SetRestitution(bodyId, restitution);
        }
    }

    vec3 PhysicsSystem::GetLinearVelocity(NodeId *node) const
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || !m_bodies[it->second].inWorld)
            return vec3(0.f);
        JPH::BodyID bodyId(m_bodies[it->second].joltBodyIdRaw);
        JPH::Vec3 v = m_joltSystem->GetBodyInterface().GetLinearVelocity(bodyId);
        return vec3(v.GetX(), v.GetY(), v.GetZ());
    }

    void PhysicsSystem::SetAngularVelocity(NodeId *node, const vec3 &vel)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || !m_bodies[it->second].inWorld)
            return;
        JPH::BodyID bodyId(m_bodies[it->second].joltBodyIdRaw);
        m_joltSystem->GetBodyInterface().SetAngularVelocity(bodyId, JPH::Vec3(vel.x, vel.y, vel.z));
    }

    vec3 PhysicsSystem::GetAngularVelocity(NodeId *node) const
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || !m_bodies[it->second].inWorld)
            return vec3(0.f);
        JPH::BodyID bodyId(m_bodies[it->second].joltBodyIdRaw);
        JPH::Vec3 v = m_joltSystem->GetBodyInterface().GetAngularVelocity(bodyId);
        return vec3(v.GetX(), v.GetY(), v.GetZ());
    }

    void PhysicsSystem::ApplyForce(NodeId *node, const vec3 &force)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || !m_bodies[it->second].inWorld)
            return;
        JPH::BodyID bodyId(m_bodies[it->second].joltBodyIdRaw);
        m_joltSystem->GetBodyInterface().AddForce(bodyId, JPH::Vec3(force.x, force.y, force.z));
    }

    void PhysicsSystem::ApplyImpulse(NodeId *node, const vec3 &impulse)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || !m_bodies[it->second].inWorld)
            return;
        JPH::BodyID bodyId(m_bodies[it->second].joltBodyIdRaw);
        m_joltSystem->GetBodyInterface().AddImpulse(bodyId, JPH::Vec3(impulse.x, impulse.y, impulse.z));
    }

    void PhysicsSystem::ApplyTorque(NodeId *node, const vec3 &torque)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || !m_bodies[it->second].inWorld)
            return;
        JPH::BodyID bodyId(m_bodies[it->second].joltBodyIdRaw);
        m_joltSystem->GetBodyInterface().AddTorque(bodyId, JPH::Vec3(torque.x, torque.y, torque.z));
    }

    // --- Raycast ---

    bool PhysicsSystem::Raycast(const vec3 &origin, const vec3 &direction, float maxDistance, RaycastResult &outResult,
                                uint32_t layerMask) const
    {
        PE_PROFILE_SCOPE("Physics Raycast");

        if (!m_joltSystem)
            return false;

        JPH::RRayCast ray;
        ray.mOrigin = JPH::Vec3(origin.x, origin.y, origin.z);
        ray.mDirection = JPH::Vec3(direction.x * maxDistance, direction.y * maxDistance, direction.z * maxDistance);

        JPH::RayCastResult hit;
        if (!m_joltSystem->GetNarrowPhaseQuery().CastRay(ray, hit, {}, LayerMaskFilter(layerMask)))
            return false;

        JPH::BodyID hitBodyId = hit.mBodyID;
        JPH::Vec3 hitPos = ray.GetPointOnRay(hit.mFraction);
        outResult.hitPoint = vec3(hitPos.GetX(), hitPos.GetY(), hitPos.GetZ());
        outResult.fraction = hit.mFraction;
        outResult.node = nullptr;

        // Single lock: resolve node (via user data) and surface normal together
        JPH::BodyLockRead lock(m_joltSystem->GetBodyLockInterface(), hitBodyId);
        if (lock.Succeeded())
        {
            outResult.node = reinterpret_cast<NodeId *>(lock.GetBody().GetUserData());
            JPH::Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPos);
            outResult.hitNormal = vec3(normal.GetX(), normal.GetY(), normal.GetZ());
        }

        return true;
    }

    void PhysicsSystem::SetTriggerEnterCallback(NodeId *node, PhysicsTriggerCallback callback)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || it->second >= m_bodies.size() || !MatchesNode(m_bodies[it->second], node))
            return;
        m_bodies[it->second].triggerEnterCallback = std::move(callback);
    }

    void PhysicsSystem::SetTriggerExitCallback(NodeId *node, PhysicsTriggerCallback callback)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || it->second >= m_bodies.size() || !MatchesNode(m_bodies[it->second], node))
            return;
        m_bodies[it->second].triggerExitCallback = std::move(callback);
    }

    void PhysicsSystem::ClearTriggerCallbacks(NodeId *node)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || it->second >= m_bodies.size() || !MatchesNode(m_bodies[it->second], node))
            return;
        m_bodies[it->second].triggerEnterCallback = nullptr;
        m_bodies[it->second].triggerExitCallback = nullptr;
    }

    void PhysicsSystem::ClearAllTriggerCallbacks()
    {
        for (auto &state : m_bodies)
        {
            state.triggerEnterCallback = nullptr;
            state.triggerExitCallback = nullptr;
            state.jointBreakCallback = nullptr;
        }
    }

    // --- Joints ---

    namespace
    {
        // Motor on a live hinge (deg/s, N m) or slider (m/s, N); other joint types have none.
        void ApplyJointMotor(JPH::TwoBodyConstraint *constraint, PhysicsJointType type, bool enabled, float speed,
                             float maxForce)
        {
            const JPH::EMotorState state = enabled ? JPH::EMotorState::Velocity : JPH::EMotorState::Off;
            if (type == PhysicsJointType::Hinge)
            {
                auto *hinge = static_cast<JPH::HingeConstraint *>(constraint);
                hinge->GetMotorSettings().SetTorqueLimit(std::max(maxForce, 0.0f));
                hinge->SetTargetAngularVelocity(glm::radians(speed));
                hinge->SetMotorState(state);
            }
            else if (type == PhysicsJointType::Slider)
            {
                auto *slider = static_cast<JPH::SliderConstraint *>(constraint);
                slider->GetMotorSettings().SetForceLimit(std::max(maxForce, 0.0f));
                slider->SetTargetVelocity(speed);
                slider->SetMotorState(state);
            }
        }
    } // namespace

    void PhysicsSystem::CreateJoint(PhysicsNodeState &state, Scene &scene)
    {
        const PhysicsJointDesc &j = state.desc.joint;
        if (j.type == PhysicsJointType::None || !state.inWorld || !m_joltSystem)
            return;
        RemoveJointOf(state.nodeId);
        const std::string &ownerName = scene.GetNodeName(state.nodeId);

        // ponytail: the connected body is the first node with that name, so two instances of a jointed prefab both
        // connect to the first one's body. Upgrade: prefer a match inside the owner's own subtree.
        const PhysicsNodeState *other = nullptr;
        if (!j.connectedNode.empty())
        {
            NodeId *otherNode = scene.FindNodeByName(j.connectedNode);
            const auto it = otherNode && otherNode != state.nodeId ? m_nodeToIndex.find(otherNode) : m_nodeToIndex.end();
            if (it == m_nodeToIndex.end() || it->second >= m_bodies.size() || !m_bodies[it->second].inWorld)
            {
                PE_WARN("[Physics] Joint on '%s': '%s' is not another physics body in the world, so no joint",
                        ownerName.c_str(), j.connectedNode.c_str());
                return;
            }
            other = &m_bodies[it->second];
        }
        if (state.desc.bodyType != PhysicsBodyType::Dynamic && !(other && other->desc.bodyType == PhysicsBodyType::Dynamic))
        {
            PE_WARN("[Physics] Joint on '%s': neither body is dynamic, so it would do nothing; no joint", ownerName.c_str());
            return;
        }

        // Pivot through the full node transform (an anchor on a scaled cube's face stays on the face); axis through
        // the rotation only.
        const mat4 &world = scene.GetWorldMatrix(state.nodeId);
        const mat3 rotation(glm::normalize(vec3(world[0])), glm::normalize(vec3(world[1])), glm::normalize(vec3(world[2])));
        const vec3 pivot = vec3(world * vec4(j.anchor, 1.0f));
        vec3 axis = vec3(0.0f, 1.0f, 0.0f);
        if (glm::length(j.axis) > 1e-6f)
            axis = glm::normalize(rotation * j.axis);
        else if (j.type == PhysicsJointType::Hinge || j.type == PhysicsJointType::Slider)
            PE_WARN("[Physics] Joint on '%s': zero axis, using +Y", ownerName.c_str());
        const JPH::RVec3 pivotJ(pivot.x, pivot.y, pivot.z);
        const JPH::Vec3 axisJ(axis.x, axis.y, axis.z);
        const JPH::Vec3 normalJ = axisJ.GetNormalizedPerpendicular();

        JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
        switch (j.type)
        {
        case PhysicsJointType::Fixed:
        {
            auto *fixed = new JPH::FixedConstraintSettings();
            fixed->mAutoDetectPoint = true; // locks the current relative pose
            settings = fixed;
            break;
        }
        case PhysicsJointType::Hinge:
        {
            auto *hinge = new JPH::HingeConstraintSettings();
            hinge->mPoint1 = hinge->mPoint2 = pivotJ;
            hinge->mHingeAxis1 = hinge->mHingeAxis2 = axisJ;
            hinge->mNormalAxis1 = hinge->mNormalAxis2 = normalJ;
            if (j.limitsEnabled) // Jolt: min in [-pi, 0], max in [0, pi]
            {
                hinge->mLimitsMin = glm::radians(std::clamp(j.limitMin, -180.0f, 0.0f));
                hinge->mLimitsMax = glm::radians(std::clamp(j.limitMax, 0.0f, 180.0f));
            }
            settings = hinge;
            break;
        }
        case PhysicsJointType::Slider:
        {
            auto *slider = new JPH::SliderConstraintSettings();
            slider->mPoint1 = slider->mPoint2 = pivotJ;
            slider->mSliderAxis1 = slider->mSliderAxis2 = axisJ;
            slider->mNormalAxis1 = slider->mNormalAxis2 = normalJ;
            if (j.limitsEnabled) // Jolt: min <= 0 <= max, from the starting position
            {
                slider->mLimitsMin = std::min(j.limitMin, 0.0f);
                slider->mLimitsMax = std::max(j.limitMax, 0.0f);
            }
            settings = slider;
            break;
        }
        case PhysicsJointType::Distance:
        {
            auto *distance = new JPH::DistanceConstraintSettings();
            const vec3 end = other ? vec3(scene.GetWorldMatrix(other->nodeId) * vec4(j.connectedAnchor, 1.0f)) : j.connectedAnchor;
            distance->mPoint1 = JPH::RVec3(end.x, end.y, end.z);
            distance->mPoint2 = pivotJ;
            if (j.limitsEnabled) // otherwise both stay -1: the length at creation, a rigid rod
            {
                distance->mMinDistance = std::max(0.0f, std::min(j.limitMin, j.limitMax));
                distance->mMaxDistance = std::max(0.0f, std::max(j.limitMin, j.limitMax));
            }
            settings = distance;
            break;
        }
        default:
            return;
        }

        // Jolt measures body 2 relative to body 1, so the owner is body 2: slider position grows along +axis and the
        // hinge angle and motor turn right-handed about the axis, as the owner sees it.
        JPH::BodyInterface &bi = m_joltSystem->GetBodyInterface();
        const JPH::BodyID bodyA(state.joltBodyIdRaw);
        const JPH::BodyID bodyB = other ? JPH::BodyID(other->joltBodyIdRaw) : JPH::BodyID(); // invalid = the world
        JPH::TwoBodyConstraint *constraint = bi.CreateConstraint(settings.GetPtr(), bodyB, bodyA);
        if (!constraint)
        {
            PE_WARN("[Physics] Joint on '%s': Jolt could not create it", ownerName.c_str());
            return;
        }
        constraint->AddRef();
        m_joltSystem->AddConstraint(constraint);
        bi.ActivateBody(bodyA);
        if (other)
            bi.ActivateBody(bodyB);
        ApplyJointMotor(constraint, j.type, j.motorEnabled, j.motorSpeed, j.motorMaxForce);
        m_joints.push_back({state.nodeId, state.joltBodyIdRaw, other ? other->joltBodyIdRaw : 0xFFFFFFFFu, constraint, j.type});
    }

    void PhysicsSystem::RemoveJointsTouching(uint32_t bodyRaw)
    {
        for (size_t i = 0; i < m_joints.size();)
        {
            if (m_joints[i].bodyA != bodyRaw && m_joints[i].bodyB != bodyRaw)
            {
                ++i;
                continue;
            }
            m_joltSystem->RemoveConstraint(m_joints[i].constraint);
            m_joints[i].constraint->Release();
            m_joints[i] = m_joints.back();
            m_joints.pop_back();
        }
    }

    void PhysicsSystem::RemoveJointOf(const NodeId *owner)
    {
        for (size_t i = 0; i < m_joints.size(); ++i)
        {
            if (m_joints[i].owner != owner)
                continue;
            m_joltSystem->RemoveConstraint(m_joints[i].constraint);
            m_joints[i].constraint->Release();
            m_joints[i] = m_joints.back();
            m_joints.pop_back();
            return; // one joint per body
        }
    }

    void PhysicsSystem::RemoveAllJoints()
    {
        for (LiveJoint &joint : m_joints)
        {
            m_joltSystem->RemoveConstraint(joint.constraint);
            joint.constraint->Release();
        }
        m_joints.clear();
    }

    void PhysicsSystem::CheckJointBreaks(float step)
    {
        // Collect, remove, then call back: a callback may add or remove joints.
        std::vector<NodeId *> broken;
        for (size_t i = 0; i < m_joints.size();)
        {
            LiveJoint &joint = m_joints[i];
            const PhysicsBodyDesc *desc = GetBodyDesc(joint.owner);
            const float limit = desc ? desc->joint.breakForce : 0.0f;
            const bool settled = joint.settled;
            joint.settled = true;
            if (limit <= 0.0f || !settled)
            {
                ++i;
                continue;
            }
            float impulse = 0.0f; // last step's positional constraint impulse
            switch (joint.type)
            {
            case PhysicsJointType::Fixed:
                impulse = static_cast<JPH::FixedConstraint *>(joint.constraint)->GetTotalLambdaPosition().Length();
                break;
            case PhysicsJointType::Hinge:
                impulse = static_cast<JPH::HingeConstraint *>(joint.constraint)->GetTotalLambdaPosition().Length();
                break;
            case PhysicsJointType::Slider:
            {
                const JPH::Vector<2> lambda = static_cast<JPH::SliderConstraint *>(joint.constraint)->GetTotalLambdaPosition();
                impulse = std::sqrt(lambda[0] * lambda[0] + lambda[1] * lambda[1]);
                break;
            }
            case PhysicsJointType::Distance:
                impulse = std::abs(static_cast<JPH::DistanceConstraint *>(joint.constraint)->GetTotalLambdaPosition());
                break;
            default:
                break;
            }
            if (impulse / step <= limit)
            {
                ++i;
                continue;
            }
            broken.push_back(joint.owner);
            m_joltSystem->RemoveConstraint(joint.constraint);
            joint.constraint->Release();
            m_joints[i] = m_joints.back();
            m_joints.pop_back();
        }
        for (NodeId *node : broken)
        {
            const auto it = m_nodeToIndex.find(node);
            if (it == m_nodeToIndex.end() || it->second >= m_bodies.size())
                continue;
            const PhysicsJointBreakCallback callback = m_bodies[it->second].jointBreakCallback; // copy: it may reset itself
            if (callback)
                callback(node);
        }
    }

    void PhysicsSystem::SetJoint(Scene &scene, NodeId *node, const PhysicsJointDesc &joint)
    {
        PhysicsBodyDesc *desc = GetBodyDesc(node);
        if (!desc)
            return;
        desc->joint = joint;
        if (joint.type == PhysicsJointType::None)
            RemoveJointOf(node);
        else if (m_simulating)
            CreateJoint(m_bodies[m_nodeToIndex.at(node)], scene);
    }

    void PhysicsSystem::RemoveJoint(NodeId *node)
    {
        if (PhysicsBodyDesc *desc = GetBodyDesc(node))
            desc->joint.type = PhysicsJointType::None;
        RemoveJointOf(node);
    }

    void PhysicsSystem::SetJointMotor(NodeId *node, bool enabled, float speed, float maxForce)
    {
        PhysicsBodyDesc *desc = GetBodyDesc(node);
        if (!desc)
            return;
        desc->joint.motorEnabled = enabled;
        desc->joint.motorSpeed = speed;
        desc->joint.motorMaxForce = maxForce;
        for (LiveJoint &joint : m_joints)
        {
            if (joint.owner != node)
                continue;
            ApplyJointMotor(joint.constraint, joint.type, enabled, speed, maxForce);
            m_joltSystem->GetBodyInterface().ActivateBody(JPH::BodyID(joint.bodyA));
        }
    }

    std::optional<float> PhysicsSystem::GetJointValue(const Scene &scene, NodeId *node) const
    {
        for (const LiveJoint &joint : m_joints)
        {
            if (joint.owner != node)
                continue;
            switch (joint.type)
            {
            case PhysicsJointType::Hinge:
                return glm::degrees(static_cast<JPH::HingeConstraint *>(joint.constraint)->GetCurrentAngle());
            case PhysicsJointType::Slider:
                return static_cast<JPH::SliderConstraint *>(joint.constraint)->GetCurrentPosition();
            case PhysicsJointType::Distance:
            {
                const PhysicsBodyDesc *desc = GetBodyDesc(node);
                if (!desc)
                    return std::nullopt;
                const vec3 a = vec3(scene.GetWorldMatrix(node) * vec4(desc->joint.anchor, 1.0f));
                const NodeId *other = scene.FindNodeByName(desc->joint.connectedNode);
                const vec3 b = other ? vec3(scene.GetWorldMatrix(other) * vec4(desc->joint.connectedAnchor, 1.0f))
                                     : desc->joint.connectedAnchor;
                return glm::length(a - b);
            }
            default:
                return 0.0f;
            }
        }
        return std::nullopt;
    }

    void PhysicsSystem::SetJointBreakCallback(NodeId *node, PhysicsJointBreakCallback callback)
    {
        auto it = m_nodeToIndex.find(node);
        if (it == m_nodeToIndex.end() || it->second >= m_bodies.size() || !MatchesNode(m_bodies[it->second], node))
            return;
        m_bodies[it->second].jointBreakCallback = std::move(callback);
    }

    // --- Simulation control ---

    void PhysicsSystem::StartSimulation(Scene &scene)
    {
        PE_PROFILE_SCOPE("Physics Start Simulation");

        if (m_simulating)
            return;

        PE_INFO("[Physics] StartSimulation: registeredBodies=%zu", m_bodies.size());
        PruneInvalidBodies(scene);

        m_simulating = true;
        m_paused = false;
        m_accumulator = 0.0f;

        // Create Jolt bodies for all registered physics nodes
        {
            PE_PROFILE_SCOPE("Physics Build Runtime Bodies");
            for (auto &state : m_bodies)
            {
                if (!state.inWorld)
                    CreateJoltBody(state, scene);
            }
            for (auto &state : m_bodies)
                CreateJoint(state, scene);
        }

        if (!m_bodies.empty())
        {
            PE_PROFILE_SCOPE("Physics Optimize Broadphase");
            m_joltSystem->OptimizeBroadPhase();
        }
    }

    void PhysicsSystem::StopSimulation()
    {
        PE_PROFILE_SCOPE("Physics Stop Simulation");

        if (!m_simulating)
        {
            ClearAllTriggerCallbacks();
            ClearTriggerContactState();
            return;
        }

        PE_INFO("[Physics] StopSimulation: registeredBodies=%zu", m_bodies.size());

        // Remove Jolt runtime bodies but keep descriptors and cached shapes
        // so the next StartSimulation can reuse them without rebuilding.
        {
            PE_PROFILE_SCOPE("Physics Destroy Runtime Bodies");
            RemoveAllJoints(); // one pass, so the per-body scan in DestroyJoltBody finds nothing
            for (auto &state : m_bodies)
            {
                if (state.inWorld)
                    DestroyJoltBody(state, false); // keep cachedShape
            }
        }

        m_simulating = false;
        m_paused = false;
        m_accumulator = 0.0f;
        ClearAllTriggerCallbacks();
        ClearTriggerContactState();
    }

    // --- Internal helpers ---

    void PhysicsSystem::PruneInvalidBodies(const Scene &scene)
    {
        PE_PROFILE_SCOPE("Physics Prune Invalid Bodies");

        if (m_bodies.empty())
        {
            m_nodeToIndex.clear();
            m_bodyIdToIndex.clear();
            ClearTriggerContactState();
            return;
        }

        std::unordered_map<const NodeId *, uint32_t> liveNodes;
        liveNodes.reserve(scene.GetNodeCount());
        for (uint32_t i = 0; i < scene.GetNodeCount(); ++i)
        {
            const NodeId *node = scene.GetNodeId(i);
            liveNodes.emplace(node, node ? node->revision : 0);
        }

        std::vector<PhysicsNodeState> kept;
        kept.reserve(m_bodies.size());
        for (auto &state : m_bodies)
        {
            bool keep = false;
            auto liveIt = liveNodes.find(state.nodeId);
            if (liveIt != liveNodes.end() && liveIt->second == state.nodeRevision)
            {
                const NodeId *node = state.nodeId;
                keep = (scene.GetComponentFlags(node) & Component_Physics) != 0;
            }

            if (keep)
            {
                kept.push_back(std::move(state));
            }
            else
            {
                if (state.inWorld)
                    DestroyJoltBody(state, true);
                else if (state.cachedShape)
                {
                    state.cachedShape->Release();
                    state.cachedShape = nullptr;
                }
            }
        }

        m_bodies = std::move(kept);
        m_nodeToIndex.clear();
        m_bodyIdToIndex.clear();
        for (size_t i = 0; i < m_bodies.size(); ++i)
        {
            m_nodeToIndex[m_bodies[i].nodeId] = i;
            if (m_bodies[i].inWorld)
                m_bodyIdToIndex[m_bodies[i].joltBodyIdRaw] = i;
        }
    }

    void PhysicsSystem::CreateJoltBody(PhysicsNodeState &state, Scene &scene)
    {
        if (state.inWorld || !m_joltSystem)
            return;

        const PhysicsBodyDesc &desc = state.desc;
        size_t bodyIndex = kInvalidBodyIndex;
        auto it = m_nodeToIndex.find(state.nodeId);
        if (it != m_nodeToIndex.end())
            bodyIndex = it->second;
        if (bodyIndex == kInvalidBodyIndex || bodyIndex >= m_bodies.size())
        {
            state.inWorld = false;
            state.joltBodyIdRaw = 0xFFFFFFFF;
            return;
        }

        if (!scene.IsNodeAlive(state.nodeId) ||
            state.nodeId->revision != state.nodeRevision ||
            !(scene.GetComponentFlags(state.nodeId) & Component_Physics))
        {
            state.inWorld = false;
            state.joltBodyIdRaw = 0xFFFFFFFF;
            return;
        }

        // Get world transform early — needed for both shape scaling and body placement.
        mat4 world = scene.GetWorldMatrix(state.nodeId);

        // Extract world scale to apply to shape dimensions
        vec3 worldScale = vec3(
            glm::length(vec3(world[0])),
            glm::length(vec3(world[1])),
            glm::length(vec3(world[2])));

        // Reuse cached shape from a previous play session if available
        JPH::RefConst<JPH::Shape> shape;
        if (state.cachedShape)
        {
            shape = state.cachedShape;
        }
        else
        {
            constexpr float minHE = 0.06f; // must exceed Jolt's default convex radius (0.05)
            switch (desc.shapeType)
            {
            case PhysicsShapeType::Box:
            {
                const vec3 he = ScaledColliderSize(desc, worldScale);
                shape = new JPH::BoxShape(JPH::Vec3(he.x, he.y, he.z));
                break;
            }
            case PhysicsShapeType::Sphere:
                shape = new JPH::SphereShape(ScaledColliderSize(desc, worldScale).x);
                break;
            case PhysicsShapeType::Capsule:
            {
                const vec3 s = ScaledColliderSize(desc, worldScale);
                shape = new JPH::CapsuleShape(s.y, s.x);
                break;
            }
            case PhysicsShapeType::ConvexHull:
            {
                const auto &refs = scene.GetNodeCache(state.nodeId).meshRefs->meshRefs;
                const auto &vertexStore = scene.GetVertexStore();
                JPH::Array<JPH::Vec3> points;
                for (int meshRef : refs)
                {
                    if (meshRef < 0)
                        continue;
                    const Mesh &mesh = scene.GetMesh(meshRef);
                    points.reserve(points.size() + mesh.vertexCount);
                    for (uint32_t i = 0; i < mesh.vertexCount && (mesh.vertexOffset + i) < vertexStore.size(); i++)
                    {
                        const auto &v = vertexStore[mesh.vertexOffset + i];
                        points.push_back(JPH::Vec3(
                            v.position[0] * worldScale.x,
                            v.position[1] * worldScale.y,
                            v.position[2] * worldScale.z));
                    }
                }
                if (!points.empty())
                {
                    // Deduplicate vertices before passing to Jolt — high-poly meshes
                    // can have thousands of duplicates that make convex hull O(n²) or worse.
                    auto cmp = [](const JPH::Vec3 &a, const JPH::Vec3 &b)
                    {
                        if (a.GetX() != b.GetX())
                            return a.GetX() < b.GetX();
                        if (a.GetY() != b.GetY())
                            return a.GetY() < b.GetY();
                        return a.GetZ() < b.GetZ();
                    };
                    std::sort(points.begin(), points.end(), cmp);
                    points.erase(std::unique(points.begin(), points.end(),
                                             [](const JPH::Vec3 &a, const JPH::Vec3 &b)
                                             {
                                                 return a.GetX() == b.GetX() && a.GetY() == b.GetY() &&
                                                        a.GetZ() == b.GetZ();
                                             }),
                                 points.end());

                    JPH::ConvexHullShapeSettings settings(points.data(), static_cast<int>(points.size()));
                    auto result = settings.Create();
                    if (result.IsValid())
                        shape = result.Get();
                }
                // Fallback to box if convex hull failed
                if (!shape)
                {
                    vec3 he = glm::max(desc.boxHalfExtents * worldScale, vec3(minHE));
                    shape = new JPH::BoxShape(JPH::Vec3(he.x, he.y, he.z));
                }
                break;
            }
            case PhysicsShapeType::Mesh:
            {
                // Static triangle collider from the node's LOD0 geometry (voxel terrain). ONE MeshShape
                // per tile (meshRef), combined into a StaticCompoundShape — not one giant mesh. Each tile
                // cooks its own ~10k-tri AABB tree (fast, and a degenerate tile can't fail the whole
                // terrain), and a future live-sculpt can re-cook only the touched tile. Static/kinematic
                // only — Jolt mesh shapes can't back a dynamic body. Verts are already world-space (the
                // host bakes world positions), so worldScale is 1 in practice, and each tile's LOD0
                // indices are mesh-local (0-based) so no cross-tile base offset is needed.
                const auto &refs = scene.GetNodeCache(state.nodeId).meshRefs->meshRefs;
                const auto &vertexStore = scene.GetVertexStore();
                const auto &indexStore = scene.GetIndexStore();
                JPH::Array<JPH::Ref<JPH::Shape>> tileShapes;
                for (int meshRef : refs)
                {
                    if (meshRef < 0)
                        continue;
                    const Mesh &mesh = scene.GetMesh(meshRef);
                    JPH::VertexList verts;
                    JPH::IndexedTriangleList tris;
                    verts.reserve(mesh.vertexCount);
                    for (uint32_t i = 0; i < mesh.vertexCount && (mesh.vertexOffset + i) < vertexStore.size(); ++i)
                    {
                        const auto &v = vertexStore[mesh.vertexOffset + i];
                        verts.push_back(JPH::Float3(v.position[0] * worldScale.x, v.position[1] * worldScale.y,
                                                    v.position[2] * worldScale.z));
                    }
                    const uint32_t io = mesh.indexOffset, ic = mesh.indexCount; // LOD0 index range
                    tris.reserve(ic / 3);
                    for (uint32_t t = 0; t + 2 < ic && (io + t + 2) < indexStore.size(); t += 3)
                        tris.push_back(JPH::IndexedTriangle(indexStore[io + t], indexStore[io + t + 1],
                                                            indexStore[io + t + 2], 0));
                    if (tris.empty())
                        continue;
                    JPH::MeshShapeSettings settings(verts, tris);
                    settings.Sanitize(); // drop degenerate/duplicate tris Jolt would assert on
                    auto result = settings.Create();
                    if (result.IsValid())
                        tileShapes.push_back(result.Get());
                    else
                        PE_WARN("[Physics] Terrain tile cook failed: %s", result.GetError().c_str());
                }
                if (tileShapes.size() == 1)
                {
                    shape = tileShapes[0]; // single tile: skip the compound wrapper (Jolt needs >=2)
                }
                else if (tileShapes.size() > 1)
                {
                    JPH::StaticCompoundShapeSettings compound;
                    for (auto &s : tileShapes)
                        compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), s.GetPtr());
                    auto result = compound.Create();
                    if (result.IsValid())
                        shape = result.Get();
                    else
                        PE_WARN("[Physics] Terrain compound cook failed: %s", result.GetError().c_str());
                }
                if (!shape) // empty/failed: tiny box keeps the body valid but harmless
                    shape = new JPH::BoxShape(JPH::Vec3(minHE, minHE, minHE));
                break;
            }
            }

            // Store for reuse on next play toggle
            state.cachedShape = shape.GetPtr();
            if (state.cachedShape)
                state.cachedShape->AddRef();
        }

        // Strip scale before extracting rotation
        vec3 pos = vec3(world[3]);
        mat3 rotMat = mat3(world);
        rotMat[0] = glm::normalize(rotMat[0]);
        rotMat[1] = glm::normalize(rotMat[1]);
        rotMat[2] = glm::normalize(rotMat[2]);
        quat rot = glm::normalize(glm::quat_cast(rotMat));

        // Determine motion type and layer
        JPH::EMotionType motionType;
        JPH::ObjectLayer layer;
        switch (desc.bodyType)
        {
        case PhysicsBodyType::Static:
            motionType = JPH::EMotionType::Static;
            break;
        case PhysicsBodyType::Kinematic:
            motionType = JPH::EMotionType::Kinematic;
            break;
        case PhysicsBodyType::Dynamic:
        default:
            motionType = JPH::EMotionType::Dynamic;
            break;
        }
        layer = Layers::ToObjectLayer(desc.layer, motionType != JPH::EMotionType::Static);

        JPH::BodyCreationSettings bodySettings(
            shape, JPH::RVec3(pos.x, pos.y, pos.z),
            JPH::Quat(rot.x, rot.y, rot.z, rot.w),
            motionType, layer);

        if (desc.bodyType == PhysicsBodyType::Dynamic)
        {
            bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bodySettings.mMassPropertiesOverride.mMass = std::max(desc.mass, 0.001f);
            // Continuous collision (swept cast) so fast/small bodies don't tunnel through thin static
            // geometry — voxel isosurface terrain is a single-sided shell, and at 1/30 s a falling body
            // moves further than its radius per step. Jolt only pays the extra cast when a body actually
            // moves >~half its inner radius in a step, so resting/slow objects cost nothing.
            // ponytail: global default for all dynamics; expose a per-body Discrete opt-out only if a
            // profile ever shows CCD is the bottleneck.
            bodySettings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        }

        bodySettings.mFriction = desc.friction;
        bodySettings.mRestitution = desc.restitution;
        bodySettings.mIsSensor = desc.isTrigger;

        // Store the node pointer as user data for reverse lookups
        bodySettings.mUserData = reinterpret_cast<uint64_t>(state.nodeId);

        JPH::BodyInterface &bi = m_joltSystem->GetBodyInterface();
        JPH::BodyID bodyId = bi.CreateAndAddBody(bodySettings, JPH::EActivation::Activate);

        if (bodyId.IsInvalid())
        {
            PE_ERROR("[Physics] Failed to create body for node '%s' (registered=%zu, max=%u)",
                     scene.GetNodeName(state.nodeId).c_str(), m_bodies.size(), kMaxBodies);
            state.inWorld = false;
            state.joltBodyIdRaw = 0xFFFFFFFF;
            return;
        }

        state.joltBodyIdRaw = bodyId.GetIndexAndSequenceNumber();
        state.inWorld = true;
        m_bodyIdToIndex[state.joltBodyIdRaw] = bodyIndex;
    }

    void PhysicsSystem::DestroyJoltBody(PhysicsNodeState &state, bool releaseShape)
    {
        if (!state.inWorld || !m_joltSystem)
            return;

        JPH::BodyID bodyId(state.joltBodyIdRaw);
        const uint32_t bodyRaw = state.joltBodyIdRaw;
        RemoveJointsTouching(bodyRaw);
        JPH::BodyInterface &bi = m_joltSystem->GetBodyInterface();
        bi.RemoveBody(bodyId);
        bi.DestroyBody(bodyId);

        m_bodyIdToIndex.erase(bodyRaw);
        ClearTriggerContactStateForBody(bodyRaw);

        state.inWorld = false;
        state.joltBodyIdRaw = 0xFFFFFFFF;

        if (releaseShape && state.cachedShape)
        {
            state.cachedShape->Release();
            state.cachedShape = nullptr;
        }
    }

    uint32_t PhysicsSystem::AddStaticMeshBody(const Vertex *verts, uint32_t vertexCount,
                                              const uint32_t *indices, uint32_t indexCount,
                                              float friction, float restitution)
    {
        if (!m_joltSystem || !verts || !indices || vertexCount == 0 || indexCount < 3)
            return 0xFFFFFFFF;

        JPH::VertexList vlist;
        JPH::IndexedTriangleList tris;
        vlist.reserve(vertexCount);
        for (uint32_t i = 0; i < vertexCount; ++i)
            vlist.push_back(JPH::Float3(verts[i].position[0], verts[i].position[1], verts[i].position[2]));
        tris.reserve(indexCount / 3);
        for (uint32_t t = 0; t + 2 < indexCount; t += 3)
            tris.push_back(JPH::IndexedTriangle(indices[t], indices[t + 1], indices[t + 2], 0));

        JPH::MeshShapeSettings settings(vlist, tris);
        settings.Sanitize(); // drop degenerate/duplicate tris Jolt would assert on
        auto result = settings.Create();
        if (!result.IsValid())
        {
            PE_WARN("[Physics] Static mesh body cook failed: %s", result.GetError().c_str());
            return 0xFFFFFFFF;
        }

        JPH::BodyCreationSettings bodySettings(result.Get(), JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
                                               JPH::EMotionType::Static, Layers::NON_MOVING);
        bodySettings.mFriction = friction;
        bodySettings.mRestitution = restitution;

        JPH::BodyInterface &bi = m_joltSystem->GetBodyInterface();
        JPH::BodyID bodyId = bi.CreateAndAddBody(bodySettings, JPH::EActivation::DontActivate);
        if (bodyId.IsInvalid())
        {
            PE_WARN("[Physics] Static mesh body create failed (raw bodies=%zu)", m_staticMeshBodies.size());
            return 0xFFFFFFFF;
        }
        const uint32_t raw = bodyId.GetIndexAndSequenceNumber();
        m_staticMeshBodies.insert(raw);
        return raw;
    }

    void PhysicsSystem::RemoveStaticMeshBody(uint32_t bodyIdRaw)
    {
        // The Jolt world owns the bodies: after Destroy() the set is already cleared, so a late caller
        // (terrain teardown ordering) simply no-ops instead of touching a dead/rebuilt world.
        if (!m_joltSystem || m_staticMeshBodies.erase(bodyIdRaw) == 0)
            return;
        JPH::BodyID bodyId(bodyIdRaw);
        JPH::BodyInterface &bi = m_joltSystem->GetBodyInterface();
        bi.RemoveBody(bodyId);
        bi.DestroyBody(bodyId);
    }

    void PhysicsSystem::SetStaticMeshBodyMaterial(uint32_t bodyIdRaw, float friction, float restitution)
    {
        if (!m_joltSystem || m_staticMeshBodies.find(bodyIdRaw) == m_staticMeshBodies.end())
            return;
        JPH::BodyID bodyId(bodyIdRaw);
        JPH::BodyInterface &bi = m_joltSystem->GetBodyInterface();
        bi.SetFriction(bodyId, friction);
        bi.SetRestitution(bodyId, restitution);
    }

    void PhysicsSystem::QueueTriggerContact(uint32_t body1Raw, uint32_t body2Raw, bool added)
    {
        if (body1Raw == 0xFFFFFFFF || body2Raw == 0xFFFFFFFF || body1Raw == body2Raw)
            return;

        std::lock_guard<std::mutex> lock(m_triggerContactMutex);
        m_queuedTriggerContacts.push_back({body1Raw, body2Raw, added});
    }

    void PhysicsSystem::DrainTriggerContacts(Scene &scene)
    {
        struct ReadyTriggerCallback
        {
            PhysicsTriggerCallback callback;
            NodeId *trigger = nullptr;
            uint32_t triggerRevision = 0;
            NodeId *other = nullptr;
            uint32_t otherRevision = 0;
        };

        std::vector<QueuedTriggerContact> contacts;
        {
            std::lock_guard<std::mutex> lock(m_triggerContactMutex);
            contacts.swap(m_queuedTriggerContacts);
        }

        if (contacts.empty())
            return;

        std::vector<ReadyTriggerCallback> readyCallbacks;

        auto resolveBody = [this](uint32_t bodyRaw) -> PhysicsNodeState *
        {
            auto it = m_bodyIdToIndex.find(bodyRaw);
            if (it == m_bodyIdToIndex.end() || it->second >= m_bodies.size())
                return nullptr;
            PhysicsNodeState &state = m_bodies[it->second];
            if (!state.inWorld || state.joltBodyIdRaw != bodyRaw)
                return nullptr;
            return &state;
        };

        auto erasePairBothWays = [this](uint32_t body1Raw, uint32_t body2Raw)
        {
            m_activeTriggerPairs.erase({body1Raw, body2Raw});
            m_activeTriggerPairs.erase({body2Raw, body1Raw});
        };

        auto handleAdded = [&](PhysicsNodeState &triggerState, PhysicsNodeState &otherState)
        {
            if (!triggerState.desc.isTrigger ||
                !IsLivePhysicsState(triggerState, scene) ||
                !IsLivePhysicsState(otherState, scene))
                return;

            TriggerPairKey key{triggerState.joltBodyIdRaw, otherState.joltBodyIdRaw};
            if (!m_activeTriggerPairs.insert(key).second || !triggerState.triggerEnterCallback)
                return;

            readyCallbacks.push_back({triggerState.triggerEnterCallback, triggerState.nodeId,
                                      triggerState.nodeRevision, otherState.nodeId, otherState.nodeRevision});
        };

        auto handleRemoved = [&](PhysicsNodeState &triggerState, PhysicsNodeState &otherState)
        {
            TriggerPairKey key{triggerState.joltBodyIdRaw, otherState.joltBodyIdRaw};
            if (m_activeTriggerPairs.erase(key) == 0 ||
                !IsLivePhysicsState(triggerState, scene) ||
                !IsLivePhysicsState(otherState, scene) ||
                !triggerState.triggerExitCallback)
                return;

            readyCallbacks.push_back({triggerState.triggerExitCallback, triggerState.nodeId,
                                      triggerState.nodeRevision, otherState.nodeId, otherState.nodeRevision});
        };

        for (const QueuedTriggerContact &contact : contacts)
        {
            PhysicsNodeState *body1 = resolveBody(contact.body1Raw);
            PhysicsNodeState *body2 = resolveBody(contact.body2Raw);
            if (!body1 || !body2)
            {
                if (!contact.added)
                    erasePairBothWays(contact.body1Raw, contact.body2Raw);
                continue;
            }

            if (contact.added)
            {
                handleAdded(*body1, *body2);
                handleAdded(*body2, *body1);
            }
            else
            {
                handleRemoved(*body1, *body2);
                handleRemoved(*body2, *body1);
            }
        }

        for (const ReadyTriggerCallback &ready : readyCallbacks)
        {
            if (!ready.callback ||
                !ready.trigger ||
                !ready.other ||
                !scene.IsNodeAlive(ready.trigger) ||
                !scene.IsNodeAlive(ready.other) ||
                ready.trigger->revision != ready.triggerRevision ||
                ready.other->revision != ready.otherRevision)
                continue;

            ready.callback(ready.trigger, ready.other);
        }
    }

    void PhysicsSystem::ClearTriggerContactState()
    {
        {
            std::lock_guard<std::mutex> lock(m_triggerContactMutex);
            m_queuedTriggerContacts.clear();
        }
        m_activeTriggerPairs.clear();
    }

    void PhysicsSystem::ClearTriggerContactStateForBody(uint32_t bodyRaw)
    {
        {
            std::lock_guard<std::mutex> lock(m_triggerContactMutex);
            m_queuedTriggerContacts.erase(
                std::remove_if(m_queuedTriggerContacts.begin(), m_queuedTriggerContacts.end(),
                               [bodyRaw](const QueuedTriggerContact &contact)
                               {
                                   return contact.body1Raw == bodyRaw || contact.body2Raw == bodyRaw;
                               }),
                m_queuedTriggerContacts.end());
        }

        for (auto it = m_activeTriggerPairs.begin(); it != m_activeTriggerPairs.end();)
        {
            if (it->triggerBodyRaw == bodyRaw || it->otherBodyRaw == bodyRaw)
                it = m_activeTriggerPairs.erase(it);
            else
                ++it;
        }
    }

    void PhysicsSystem::SyncTransformsFromJolt(Scene &scene)
    {
        PE_PROFILE_SCOPE("Physics Sync Body Transforms");

        m_syncWorldMats.clear();
        m_syncParentInv.clear();
        m_syncChanged.clear();

        JPH::BodyInterface &bi = m_joltSystem->GetBodyInterface();

        for (auto &state : m_bodies)
        {
            if (!state.inWorld || state.desc.bodyType == PhysicsBodyType::Static)
                continue;

            JPH::BodyID bodyId(state.joltBodyIdRaw);
            if (state.desc.bodyType == PhysicsBodyType::Dynamic && !bi.IsActive(bodyId))
                continue;

            JPH::RVec3 joltPos;
            JPH::Quat joltRot;
            bi.GetPositionAndRotation(bodyId, joltPos, joltRot);

            vec3 pos(joltPos.GetX(), joltPos.GetY(), joltPos.GetZ());
            quat rot(joltRot.GetW(), joltRot.GetX(), joltRot.GetY(), joltRot.GetZ());

            // A physics body on a CAMERA node drives the camera's world position — it just fell /
            // collided / was pushed by a script's forces this frame. The camera is normally
            // authoritative over its node (Scene::UpdateCameras writes camera pos->node), which would
            // otherwise clobber this. We set only position, never rotation, so mouse/script look stays
            // camera-authoritative; UpdateCameras then writes (this body pos, camera euler) to the node.
            if (Camera *cam = scene.GetCameraForNode(state.nodeId))
                cam->SetPosition(pos);

            // Use cached authored scale — avoids 3x glm::length per body per frame
            const mat4 &oldLocal = scene.GetLocalMatrix(state.nodeId);

            // Build world matrix from physics (translation * rotation * scale)
            mat4 worldMat = glm::translate(mat4(1.f), pos) * glm::mat4_cast(rot) *
                            glm::scale(mat4(1.f), state.authoredScale);
            m_syncWorldMats[state.nodeId] = worldMat;

            NodeId *parent = scene.GetParent(state.nodeId);
            mat4 newLocal;
            if (parent)
            {
                auto parentInvIt = m_syncParentInv.find(parent);
                if (parentInvIt == m_syncParentInv.end())
                {
                    const mat4 &parentWorld = [&]() -> const mat4 &
                    {
                        auto updatedIt = m_syncWorldMats.find(parent);
                        if (updatedIt != m_syncWorldMats.end())
                            return updatedIt->second;
                        return scene.GetWorldMatrix(parent);
                    }();

                    parentInvIt = m_syncParentInv.emplace(parent, glm::inverse(parentWorld)).first;
                }
                newLocal = parentInvIt->second * worldMat;
            }
            else
            {
                newLocal = worldMat;
            }

            if (!MatricesNearEqual(newLocal, oldLocal))
            {
                scene.SetLocalMatrix(state.nodeId, newLocal, false);
                m_syncChanged.push_back(state.nodeId);
            }
        }

        if (m_syncChanged.empty())
            return;

        m_syncChangedSet.clear();
        for (NodeId *node : m_syncChanged)
            m_syncChangedSet.insert(node);

        for (NodeId *node : m_syncChanged)
        {
            NodeId *parent = scene.GetParent(node);
            if (!parent || m_syncChangedSet.count(parent) == 0)
                scene.MarkNodeDirty(node);
        }
    }
} // namespace pe

#endif // PE_PHYSICS
