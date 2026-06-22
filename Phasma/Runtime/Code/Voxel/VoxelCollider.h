#pragma once
#include "Voxel/VoxelTypes.h"
#include "Base/Math.h"

// CPU collision + ray-pick against the voxel grid. Free functions, no Scene/World deps
// (the isSolid predicate abstracts the world) so they are unit-testable.
// FROZEN interface (Wave-1 contract).
namespace pe::voxel
{
    struct VoxelRayHit
    {
        bool hit;
        BlockPos cell;     // the solid cell hit
        BlockPos adjacent; // the empty cell on the near side of the hit face (place target)
        vec3 normal;       // face normal pointing back toward the ray origin
        float dist;
    };

    // Amanatides-Woo DDA voxel ray-march to maxDist.
    VoxelRayHit RaycastVoxels(const vec3 &origin, const vec3 &dir, float maxDist,
                              const std::function<bool(int, int, int)> &isSolid);

    // Swept-AABB move with per-axis slide resolve against solid blocks. Returns the resolved
    // new position after sliding. (Per-axis resolve order is documented in the .cpp.)
    vec3 MoveAabb(const vec3 &pos, const vec3 &halfExtents, const vec3 &delta,
                  const std::function<bool(int, int, int)> &isSolid);
} // namespace pe::voxel
