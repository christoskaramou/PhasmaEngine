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
    };
} // namespace pe
