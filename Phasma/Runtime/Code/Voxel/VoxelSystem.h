#pragma once

#include "ECS/System.h"
#include "Voxel/VoxelWorld.h"

namespace pe
{
    class Scene;
}

namespace pe::voxel
{
    class VoxelSystem : public ISystem
    {
    public:
        VoxelSystem() = default;
        ~VoxelSystem() override;

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;

        VoxelWorld *World();
        VoxelWorld *CreateWorld(Scene *scene, const VoxelConfig &cfg);

    private:
        // Drive the world from the scene's "Voxel World" node (NodeVoxelWorldTag): create it when the
        // node appears/enables, recreate on structural config change, retune LOD live, destroy when
        // the node disables/vanishes. Script-created worlds (voxel.create) are left untouched.
        void ReconcileComponentWorld();

        std::unique_ptr<VoxelWorld> m_world;
        bool m_componentOwned = false;     // the live world came from ReconcileComponentWorld
        uint64_t m_appliedHash = 0;        // structural-config hash of the component world
        int m_appliedLod0Radius = 0;       // lod0Radius applied (retuned live, outside the hash)
        bool m_appliedPhysics = false;     // physics collider state applied (toggled live, outside the hash)
        float m_appliedFriction = 0.5f;    // terrain collider friction applied (live, outside the hash)
        float m_appliedRestitution = 0.3f; // terrain collider restitution applied (live, outside the hash)
        bool m_wasSimulating = false;      // last-seen PhysicsSystem sim state; re-register the terrain
                                           // collider on the false->true edge (play start re-churns the host)
        uint64_t m_pendingHash = 0;        // last-seen structural hash (recreate debounce)
        int m_pendingFrames = 0;           // frames the pending hash has been stable
    };
} // namespace pe::voxel
