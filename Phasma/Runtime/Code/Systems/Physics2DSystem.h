#pragma once

#ifdef PE_PHYSICS2D

namespace pe
{
    struct NodeId;
    class Scene;

    enum class Physics2DBodyType
    {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class Physics2DShapeType
    {
        Box,
        Circle,
        Capsule,
    };

    struct Physics2DBodyDesc
    {
        Physics2DBodyType bodyType = Physics2DBodyType::Dynamic;
        Physics2DShapeType shapeType = Physics2DShapeType::Box;
        float width = 1.0f;
        float height = 1.0f;
        float radius = 0.5f;
        float capsuleHeight = 1.0f;
        float capsuleRadius = 0.25f;
        float density = 1.0f;
        float friction = 0.5f;
        float restitution = 0.0f;
        float linearDamping = 0.0f;
        float angularDamping = 0.0f;
        float gravityScale = 1.0f;
        uint64_t categoryBits = 1u;
        uint64_t maskBits = UINT64_MAX;
        int groupIndex = 0;
        bool fixedRotation = false;
        bool isSensor = false;
        bool syncNode = true;
        bool bullet = false;
        bool enableSleep = true;
    };

    struct Physics2DContactEvent
    {
        uint32_t bodyA = 0;
        uint32_t bodyB = 0;
        bool began = false;
        bool sensor = false;
        bool hit = false;
        vec2 point = vec2(0.0f);
        vec2 normal = vec2(0.0f);
        float approachSpeed = 0.0f;
    };

    // Per-node sensor enter/exit callback (trigger = the sensor's node, other = the visiting body's node).
    // Fired on the main thread AFTER the step (never mid-step), mirroring the 3D PhysicsSystem.
    using Physics2DTriggerCallback = std::function<void(NodeId *trigger, NodeId *other)>;

    class Physics2DSystem : public ISystem
    {
    public:
        Physics2DSystem();
        ~Physics2DSystem() override;

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;

        uint32_t AddBoxBody(Scene &scene, NodeId *node, Physics2DBodyType bodyType, float width, float height,
                            const Physics2DBodyDesc &desc = {});
        uint32_t AddCircleBody(Scene &scene, NodeId *node, Physics2DBodyType bodyType, float radius,
                               const Physics2DBodyDesc &desc = {});
        uint32_t AddCapsuleBody(Scene &scene, NodeId *node, Physics2DBodyType bodyType, float height, float radius,
                                const Physics2DBodyDesc &desc = {});
        uint32_t AddBody(Scene &scene, NodeId *node, const Physics2DBodyDesc &desc);

        void RemoveBody(uint32_t bodyId);
        void RemoveBody(NodeId *node);
        Physics2DBodyDesc *GetBodyDesc(NodeId *node);
        const Physics2DBodyDesc *GetBodyDesc(const NodeId *node) const;
        bool HasBody(uint32_t bodyId) const;
        bool HasBody(const NodeId *node) const;
        uint32_t GetBodyId(const NodeId *node) const;
        void ClearWorld();

        void SetPaused(bool paused) { m_paused = paused; }
        bool IsPaused() const { return m_paused; }

        void SetGravity(const vec2 &gravity);
        vec2 GetGravity() const;

        void SetLinearVelocity(uint32_t bodyId, const vec2 &velocity);
        vec2 GetLinearVelocity(uint32_t bodyId) const;
        void SetAngularVelocity(uint32_t bodyId, float velocity);
        float GetAngularVelocity(uint32_t bodyId) const;
        void ApplyForce(uint32_t bodyId, const vec2 &force);
        void ApplyImpulse(uint32_t bodyId, const vec2 &impulse);
        void SetTransform(uint32_t bodyId, const vec2 &position, float angleRadians);
        bool GetTransform(uint32_t bodyId, vec2 &position, float &angleRadians) const;
        void SetNodeSync(uint32_t bodyId, bool enabled);

        const std::vector<Physics2DContactEvent> &GetContactEvents() const { return m_contactEvents; }
        void ClearContactEvents() { m_contactEvents.clear(); }

        // Sensor trigger callbacks (used by trigger-zone Physics sections); invoked after the step.
        void SetTriggerEnterCallback(NodeId *node, Physics2DTriggerCallback callback);
        void SetTriggerExitCallback(NodeId *node, Physics2DTriggerCallback callback);
        void ClearTriggerCallbacks(NodeId *node);

    private:
        struct Impl;
        struct BodyState;

        BodyState *FindBody(uint32_t bodyId);
        const BodyState *FindBody(uint32_t bodyId) const;
        void CreateWorld();
        void DestroyWorld();
        void PruneInvalidBodies(Scene &scene);
        void SyncTransformsToNodes(Scene &scene);
        void CollectEvents();

        std::unique_ptr<Impl> m_impl;
        // Sensor enter/exit callbacks queued during the step, fired after it (re-entrancy-safe).
        struct PendingZoneTrigger
        {
            Physics2DTriggerCallback callback;
            NodeId *trigger = nullptr;
            uint32_t triggerRevision = 0;
            NodeId *other = nullptr;
            uint32_t otherRevision = 0;
        };

        std::unordered_map<uint32_t, std::unique_ptr<BodyState>> m_bodies;
        std::unordered_map<const NodeId *, uint32_t> m_nodeToBody;
        std::vector<Physics2DContactEvent> m_contactEvents;
        std::vector<PendingZoneTrigger> m_pendingZoneTriggers;
        vec2 m_gravity = vec2(0.0f);
        uint32_t m_nextBodyId = 1;
        float m_accumulator = 0.0f;
        bool m_paused = false;

        static constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
        static constexpr int MAX_STEPS_PER_FRAME = 4;
        static constexpr int SUBSTEP_COUNT = 4;
    };
} // namespace pe

#endif // PE_PHYSICS2D
