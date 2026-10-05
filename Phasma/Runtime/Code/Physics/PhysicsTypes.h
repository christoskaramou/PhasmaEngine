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
