#pragma once

namespace pe
{
    enum class PhysicsBodyType : uint8_t
    {
        Static = 0,
        Dynamic,
        Kinematic
    };

    enum class PhysicsShapeType : uint8_t
    {
        Box = 0,
        Sphere,
        Capsule,
        ConvexHull,
        Mesh // exact triangle mesh from the node's mesh refs; static/kinematic only (e.g. voxel terrain)
    };

    enum class PhysicsJointType : uint8_t
    {
        None = 0,
        Fixed,
        Hinge,
        Distance,
        Slider
    };

    // A joint from this body to another node's body (by name) or to the world (empty name); one per body. Hinge
    // angle and slider position are measured from the pose when the joint is created (play start).
    struct PhysicsJointDesc
    {
        PhysicsJointType type = PhysicsJointType::None;
        std::string connectedNode;        // empty = fixed to the world
        vec3 anchor = vec3(0.f);          // pivot, in this node's local space (scale included)
        vec3 axis = vec3(0.f, 1.f, 0.f);  // hinge / slider axis, in this node's local space
        vec3 connectedAnchor = vec3(0.f); // Distance: the other end, local to the connected node (world if none)
        bool limitsEnabled = false;       // Distance without limits keeps its starting length
        float limitMin = -90.f;           // hinge degrees, slider and distance metres
        float limitMax = 90.f;
        bool motorEnabled = false;    // hinge and slider
        float motorSpeed = 0.f;       // hinge deg/s, slider m/s
        float motorMaxForce = 1000.f; // hinge N m, slider N
        float breakForce = 0.f;       // N; 0 = unbreakable
    };

    struct PhysicsBodyDesc
    {
        PhysicsBodyType bodyType = PhysicsBodyType::Dynamic;
        PhysicsShapeType shapeType = PhysicsShapeType::Box;
        float mass = 1.0f;
        float friction = 0.5f;
        float restitution = 0.3f;
        vec3 boxHalfExtents = vec3(0.5f);
        float sphereRadius = 0.5f;
        float capsuleHalfHeight = 0.5f;
        float capsuleRadius = 0.25f;
        bool autoFitShape = true;
        bool isTrigger = false;
        uint8_t layer = 0; // index into SceneSettings::physics_layer_names
        PhysicsJointDesc joint;
    };

    // Box/Sphere/Capsule size after the node's world scale, exactly as the Jolt shape is built: box half
    // extents; sphere radius in x; capsule radius in x and cylinder half height in y. Floored above Jolt's
    // 0.05 convex radius. The editor's collider overlay draws from this too.
    inline vec3 ScaledColliderSize(const PhysicsBodyDesc &desc, const vec3 &worldScale)
    {
        constexpr float minHE = 0.06f;
        switch (desc.shapeType)
        {
        case PhysicsShapeType::Sphere:
            return vec3(std::max(desc.sphereRadius * std::max({worldScale.x, worldScale.y, worldScale.z}), minHE));
        case PhysicsShapeType::Capsule:
            return vec3(std::max(desc.capsuleRadius * std::max(worldScale.x, worldScale.z), minHE),
                        std::max(desc.capsuleHalfHeight * worldScale.y, minHE), 0.f);
        default:
            return glm::max(desc.boxHalfExtents * worldScale, vec3(minHE));
        }
    }
} // namespace pe
