#pragma once

#include "Base/Math.h"
#include "Voxel/BlockRegistry.h"
#include "Voxel/ChunkColumn.h"
#include "Voxel/GeometryArena.h"
#include "Voxel/VoxelTypes.h"

namespace pe
{
    class CommandBuffer;
    class Material;
    class Scene;
    struct NodeId;
} // namespace pe

namespace pe::voxel
{
    class VoxelMaterial;

    struct VoxelConfig
    {
        int loadRadius = 4;
        int uploadBudgetPerFrame = 8;
        int groundY = 64;
    };

    class VoxelWorld
    {
    public:
        VoxelWorld();
        ~VoxelWorld();

        void Create(Scene *scene, const VoxelConfig &cfg);
        void Destroy();
        void SetAnchor(const vec3 &worldPos);
        BlockId GetBlock(int x, int y, int z) const;
        void SetBlock(int x, int y, int z, BlockId id);
        bool Raycast(const vec3 &o, const vec3 &d, float maxDist,
                     BlockPos &hit, BlockPos &adjacent, vec3 &normal) const;
        void Update();
        BlockRegistry &Registry();

    private:
        struct SectionHandle
        {
            ColumnCoord coord{};
            int sectionIndex = 0;
            ArenaHandle handle{};
        };

        static uint64_t ColumnKey(ColumnCoord coord);
        ChunkColumn *FindColumn(ColumnCoord coord);
        const ChunkColumn *FindColumn(ColumnCoord coord) const;
        void RegisterDefaultBlocks();
        void CreateHostMesh();
        void UploadInitialGrid();
        void RetireSubmittedUpdateCommands(bool all);

        Scene *m_scene = nullptr;
        VoxelConfig m_cfg{};
        vec3 m_anchor = vec3(0.0f);
        BlockRegistry m_registry;
        GeometryArena m_arena;
        std::unordered_map<uint64_t, ChunkColumn> m_columns;
        std::vector<SectionHandle> m_sections;
        std::vector<CommandBuffer *> m_submittedUpdateCmds;
        std::unique_ptr<Material> m_hostMaterial;
        std::unique_ptr<VoxelMaterial> m_voxelMaterial;
        NodeId *m_hostNode = nullptr;
        int m_hostMeshIndex = -1;
        uint32_t m_hostDataOffset = 0xFFFFFFFF;
        uint32_t m_materialGpuIndex = 0xFFFFFFFF;
    };
} // namespace pe::voxel
