#pragma once

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
    class ITerrainGenerator;

    // Horizontal neighbor column snapshots for seam-aware greedy meshing.
    struct ColumnNeighbors
    {
        std::shared_ptr<const ChunkColumn> negX;
        std::shared_ptr<const ChunkColumn> posX;
        std::shared_ptr<const ChunkColumn> negZ;
        std::shared_ptr<const ChunkColumn> posZ;
        std::shared_ptr<const ChunkColumn> negXnegZ;
        std::shared_ptr<const ChunkColumn> negXposZ;
        std::shared_ptr<const ChunkColumn> posXnegZ;
        std::shared_ptr<const ChunkColumn> posXposZ;
    };

    struct VoxelConfig
    {
        int loadRadius = 8;
        int unloadMargin = 2;
        int uploadBudgetPerFrame = 4;
        int groundY = 64;
        std::string saveDir; // relative to Assets or absolute; empty disables persistence
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
        bool SaveAllModified();
        // Override the world's terrain generator. Call before Create(); the engine otherwise
        // installs a default NoiseGen. Pass nullptr to revert to the default. Generators run on
        // worker threads, so the implementation must be thread-safe.
        void SetTerrainGenerator(std::shared_ptr<ITerrainGenerator> generator);

    private:
        enum class ColumnLoadState
        {
            Empty,
            Generating,
            Generated,
            Meshing,
            Ready,
            Unloading,
        };

        struct PendingEdit
        {
            int x = 0;
            int y = 0;
            int z = 0;
            BlockId id = kAir;
        };

        struct ColumnState
        {
            ColumnCoord coord{};
            ColumnLoadState state = ColumnLoadState::Empty;
            std::shared_future<ChunkColumn> generationFuture;
            std::unique_ptr<ChunkColumn> column;
            std::array<std::shared_future<MeshData>, kSectionCount> meshFutures;
            std::array<std::shared_future<MeshData>, kSectionCount> remeshFutures;
            std::array<ArenaHandle, kSectionCount> handles{};
            std::array<bool, kSectionCount> sectionUploaded{};
            std::array<bool, kSectionCount> remeshPending{};
            std::array<bool, kSectionCount> dirtyAfterRemesh{};
            uint16_t touchedSectionMask = 0; // sections edited or loaded from disk; drives save
        };

        static uint64_t ColumnKey(ColumnCoord coord);
        static ColumnCoord AnchorToColumn(const vec3 &worldPos);
        static int ColumnDistance(ColumnCoord a, ColumnCoord b);
        ColumnState *FindColumnState(ColumnCoord coord);
        const ColumnState *FindColumnState(ColumnCoord coord) const;
        ChunkColumn *FindColumn(ColumnCoord coord);
        const ChunkColumn *FindColumn(ColumnCoord coord) const;
        void RegisterDefaultBlocks();
        void CreateHostMesh();
        void RequestColumnsForAnchor();
        void EnqueueColumnGeneration(ColumnCoord coord);
        void ProcessGenerationResults();
        void ApplyPendingEdits(uint64_t key, ChunkColumn &column);
        void EnqueueColumnMeshing(ColumnState &state);
        bool NeighborGenerationInProgress(ColumnCoord coord) const;
        void TryStartColumnMeshing(ColumnState &state);
        void ProcessPendingMeshing();
        int ProcessReadyMeshUploads(CommandBuffer *cmd, int budget);
        ArenaHandle UploadSectionMesh(CommandBuffer *cmd, ColumnCoord coord, int si, const MeshData &mesh);
        void ReleaseColumn(ColumnState &state);
        void StartDirtySectionRemesh(ColumnState &state, int si);
        void ProcessDirtyRemeshResults(CommandBuffer *cmd);
        void RemeshDirtySections(CommandBuffer *cmd);
        void MarkSectionDirty(ColumnCoord coord, int si);
        void MarkEditDirtySections(ColumnCoord coord, int wx, int y, int wz);
        void RetireSubmittedUpdateCommands(bool all);

        ColumnNeighbors GatherNeighborSnapshots(ColumnCoord coord) const;
        void RemeshNeighborSeams(ColumnCoord coord);
        void TouchSection(ColumnCoord coord, int si);
        void PersistColumnIfTouched(ColumnState &state);
        void PersistAllTouchedColumns();

        Scene *m_scene = nullptr;
        VoxelConfig m_cfg{};
        std::filesystem::path m_saveRoot;
        std::shared_ptr<ITerrainGenerator> m_generator; // default NoiseGen, or a game-supplied override
        bool m_generatorOverridden = false;             // true once SetTerrainGenerator set a non-null gen
        vec3 m_anchor = vec3(0.0f);
        BlockRegistry m_registry;
        GeometryArena m_arena;
        std::unordered_map<uint64_t, ColumnState> m_columns;
        std::unordered_map<uint64_t, std::vector<PendingEdit>> m_pendingEdits;
        std::vector<std::pair<uint64_t, int>> m_dirtySections; // (ColumnKey, sectionIndex) pending remesh
        std::vector<CommandBuffer *> m_submittedUpdateCmds;
        std::unique_ptr<Material> m_hostMaterial;
        std::unique_ptr<VoxelMaterial> m_voxelMaterial;
        NodeId *m_hostNode = nullptr;
        int m_hostMeshIndex = -1;
        uint32_t m_hostDataOffset = 0xFFFFFFFF;
        uint32_t m_materialGpuIndex = 0xFFFFFFFF;
    };
} // namespace pe::voxel
