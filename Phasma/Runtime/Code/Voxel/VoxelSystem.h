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
        std::unique_ptr<VoxelWorld> m_world;
    };
} // namespace pe::voxel
