#include "Voxel/VoxelSystem.h"

namespace pe::voxel
{
    VoxelSystem::~VoxelSystem()
    {
        Destroy();
    }

    void VoxelSystem::Init(CommandBuffer *)
    {
        SetEnabled(true);
    }

    void VoxelSystem::Update()
    {
        if (m_world)
            m_world->Update();
    }

    void VoxelSystem::Destroy()
    {
        if (m_world)
        {
            m_world->Destroy();
            m_world.reset();
        }
        SetEnabled(false);
    }

    VoxelWorld *VoxelSystem::World()
    {
        return m_world.get();
    }

    VoxelWorld *VoxelSystem::CreateWorld(Scene *scene, const VoxelConfig &cfg)
    {
        if (!m_world)
            m_world = std::make_unique<VoxelWorld>();
        else
            m_world->Destroy();

        m_world->Create(scene, cfg);
        SetEnabled(true);
        // Bootstrap registration via CreateGlobalSystem<VoxelSystem>() is wired by PlayerHost.cpp's orchestrator.
        return m_world.get();
    }
} // namespace pe::voxel
