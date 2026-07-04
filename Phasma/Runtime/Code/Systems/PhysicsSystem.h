#pragma once

#ifdef PE_PHYSICS

#include "Physics/PhysicsTypes.h"

// Forward-declare Jolt types to avoid header pollution
namespace JPH
{
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class Body;
    class BodyID;
    class Shape;
} // namespace JPH

namespace pe
{
    struct NodeId;
    class Scene;
    class TriggerContactListener;

    using PhysicsTriggerCallback = std::function<void(NodeId *trigger, NodeId *other)>;

    struct PhysicsNodeState
    {
        NodeId *nodeId = nullptr;
        uint32_t nodeRevision = 0;
        PhysicsBodyDesc desc;
        vec3 authoredScale = vec3(1.f);          // cached at AddBody, avoids per-frame glm::length extraction
        const JPH::Shape *cachedShape = nullptr; // ref-counted manually in .cpp; reused across play toggles
        uint32_t joltBodyIdRaw = 0xFFFFFFFF;     // JPH::BodyID raw value
        bool inWorld = false;
        PhysicsTriggerCallback triggerEnterCallback;
        PhysicsTriggerCallback triggerExitCallback;
    };

    struct RaycastResult
    {
        NodeId *node = nullptr;
        vec3 hitPoint;
        vec3 hitNormal;
        float fraction = 1.0f;
    };

    class PhysicsSystem : public ISystem
    {
    public:
        ~PhysicsSystem() override;

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;

        // Per-node body management
        void AddBody(Scene &scene, NodeId *node, const PhysicsBodyDesc &desc);
        void RemoveBody(NodeId *node);
        PhysicsBodyDesc *GetBodyDesc(NodeId *node);
        const PhysicsBodyDesc *GetBodyDesc(const NodeId *node) const;
        bool HasBody(const NodeId *node) const;
        void ClearAllBodies();
        void InvalidateShapeCache(NodeId *node);
        void NotifyScaleChanged(Scene &scene, NodeId *node);
        // Update a body's friction/restitution live (no shape re-cook); no-op if the body isn't in-world.
        void SetBodyMaterial(NodeId *node, float friction, float restitution);

        // Runtime API (during simulation)
        void SetLinearVelocity(NodeId *node, const vec3 &vel);
        vec3 GetLinearVelocity(NodeId *node) const;
        void SetAngularVelocity(NodeId *node, const vec3 &vel);
        vec3 GetAngularVelocity(NodeId *node) const;
        void ApplyForce(NodeId *node, const vec3 &force);
        void ApplyImpulse(NodeId *node, const vec3 &impulse);
        void ApplyTorque(NodeId *node, const vec3 &torque);

        // Raycast
        bool Raycast(const vec3 &origin, const vec3 &direction, float maxDistance, RaycastResult &outResult) const;

        // Trigger callbacks are invoked on the main thread after the physics step.
        void SetTriggerEnterCallback(NodeId *node, PhysicsTriggerCallback callback);
        void SetTriggerExitCallback(NodeId *node, PhysicsTriggerCallback callback);
        void ClearTriggerCallbacks(NodeId *node);
        void ClearAllTriggerCallbacks();

        // Simulation control
        void StartSimulation(Scene &scene);
        void StopSimulation();
        void SetPaused(bool paused) { m_paused = paused; }
        bool IsSimulating() const { return m_simulating; }

    private:
        void PruneInvalidBodies(const Scene &scene);
        void CreateJoltBody(PhysicsNodeState &state, Scene &scene);
        void DestroyJoltBody(PhysicsNodeState &state, bool releaseShape = true);
        void SyncTransformsFromJolt(Scene &scene);
        void QueueTriggerContact(uint32_t body1Raw, uint32_t body2Raw, bool added);
        void DrainTriggerContacts(Scene &scene);
        void ClearTriggerContactState();
        void ClearTriggerContactStateForBody(uint32_t bodyRaw);

        friend class TriggerContactListener;

        struct QueuedTriggerContact
        {
            uint32_t body1Raw = 0xFFFFFFFF;
            uint32_t body2Raw = 0xFFFFFFFF;
            bool added = false;
        };

        struct TriggerPairKey
        {
            uint32_t triggerBodyRaw = 0xFFFFFFFF;
            uint32_t otherBodyRaw = 0xFFFFFFFF;

            bool operator==(const TriggerPairKey &other) const
            {
                return triggerBodyRaw == other.triggerBodyRaw && otherBodyRaw == other.otherBodyRaw;
            }
        };

        struct TriggerPairKeyHash
        {
            size_t operator()(const TriggerPairKey &key) const
            {
                const uint64_t packed = (static_cast<uint64_t>(key.triggerBodyRaw) << 32u) |
                                        static_cast<uint64_t>(key.otherBodyRaw);
                return std::hash<uint64_t>{}(packed);
            }
        };

        std::unordered_map<const NodeId *, size_t> m_nodeToIndex;
        std::unordered_map<uint32_t, size_t> m_bodyIdToIndex;
        std::vector<PhysicsNodeState> m_bodies;

        // Persistent scratch — reused every frame to avoid per-frame heap alloc/dealloc
        std::unordered_map<const NodeId *, mat4> m_syncWorldMats;
        std::unordered_map<const NodeId *, mat4> m_syncParentInv;
        std::vector<NodeId *> m_syncChanged;
        std::unordered_set<const NodeId *> m_syncChangedSet;

        // Jolt subsystems (raw pointers — created in Init, destroyed in Destroy)
        JPH::TempAllocatorImpl *m_tempAllocator = nullptr;
        JPH::JobSystemThreadPool *m_jobSystem = nullptr;
        JPH::PhysicsSystem *m_joltSystem = nullptr;
        TriggerContactListener *m_contactListener = nullptr;

        std::mutex m_triggerContactMutex;
        std::vector<QueuedTriggerContact> m_queuedTriggerContacts;
        std::unordered_set<TriggerPairKey, TriggerPairKeyHash> m_activeTriggerPairs;

        bool m_simulating = false;
        bool m_paused = false;
        float m_accumulator = 0.0f;

        static constexpr float FIXED_TIMESTEP = 1.0f / 30.0f;
        static constexpr int MAX_STEPS_PER_FRAME = 2;
    };
} // namespace pe

#endif // PE_PHYSICS
