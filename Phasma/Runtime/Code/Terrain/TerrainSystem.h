#pragma once

#include "ECS/System.h"
#include "Terrain/TerrainWorld.h"

namespace pe
{
    class Scene;
} // namespace pe

namespace pe::terrain
{
    class TerrainSystem : public ISystem
    {
    public:
        TerrainSystem() = default;
        ~TerrainSystem() override;

        void Init(CommandBuffer *cmd) override;
        void Update() override;
        void Destroy() override;

        TerrainWorld *World();
        // ops (tag/serializer flat 7-float format) seed the new world before its Create-time meshing.
        TerrainWorld *CreateWorld(Scene *scene, const TerrainConfig &cfg,
                                  const std::vector<float> *ops = nullptr);

    private:
        // Drive the terrain from the scene's "Terrain" node (NodeTerrainTag): build it when the node
        // appears/enables, rebuild on config change (debounced), toggle the collider live, destroy when
        // the node disables/vanishes. Script-created worlds (terrain.create) are left untouched.
        void ReconcileComponentWorld();

        std::unique_ptr<TerrainWorld> m_world;
        bool m_componentOwned = false;
        uint64_t m_appliedHash = 0;
        bool m_appliedPhysics = false;
        float m_appliedFriction = 0.5f;
        float m_appliedRestitution = 0.3f;
        bool m_wasSimulating = false; // re-register the collider on the sim false->true edge
        uint64_t m_pendingHash = 0;
        int m_pendingFrames = 0;
    };
} // namespace pe::terrain
