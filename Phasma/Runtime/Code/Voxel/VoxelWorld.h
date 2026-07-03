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
        // Full-detail radius in columns; each lod0Radius further drops one mip (max lod 2, so 4-block
        // cells). 0 disables LOD — every column meshes at full detail regardless of distance.
        int lod0Radius = 0;
        // Total world bound in columns around (boundsCenterCx, boundsCenterCz); columns outside never
        // generate (queries there read air). 0 = infinite in X/Z.
        int worldRadius = 0;
        int boundsCenterCx = 0;
        int boundsCenterCz = 0;
        // false = fixed grid at the bounds center (worldRadius, or loadRadius when unbounded); the
        // anchor is ignored and nothing ever unloads.
        bool streaming = true;
        std::string saveDir; // relative to Assets or absolute; empty disables persistence

        // --- Worldgen (any change rebuilds the world) ---
        // Noise terrain knobs, used while heightmapPath is empty (defaults = the historical look):
        float noiseAmplitude = 28.0f;    // peak height above groundY in blocks; 0 = flat plain
        float noiseFeatureScale = 96.0f; // base feature wavelength in blocks (bigger = rolling hills)
        int noiseSeed = 0;               // shifts the noise domain; each seed is a different world
        bool caves = true;
        // Water surface height: <0 = auto (groundY - 2), 0 = no water, >0 = absolute blocks.
        int seaLevel = -1;
        // Heightmap worldgen, used when heightmapPath is set (see MapGen.h): grayscale surface map
        // (pixel 0..255 == height in blocks) + optional strata-thickness maps, all Assets-relative
        // or absolute, centered on the bounds center. A bounded non-streaming world with
        // worldRadius 0 derives its radius from the map extent.
        std::string heightmapPath;
        std::string strata1Path;
        std::string strata2Path;
        int blocksPerPixel = 1; // one map pixel spans this many blocks in X/Z
        int surfaceBlock = 3;   // block ids: 1=stone 2=dirt 3=grass 4=water, 0=air
        int strata1Block = 2;
        int strata2Block = 1;
        int fillBlock = 1;        // fills below the strata to y=0; 0 = air (floating shells)
        int strata1Thickness = 3; // fixed thickness when the strata map is absent
        int strata2Thickness = 8;
    };

    class VoxelWorld
    {
    public:
        VoxelWorld();
        ~VoxelWorld();

        void Create(Scene *scene, const VoxelConfig &cfg);
        void Destroy();
        void SetAnchor(const vec3 &worldPos);
        // Columns in coarse LOD bands hold coarse-generated data (cell-resolution heights, no caves)
        // until their band drops — GetBlock/Raycast are approximate out there. SetBlock into such a
        // column queues the edit and regenerates full-detail data first, so edits are never lost.
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
        // Live LOD retune (0 = off): updates the config and remeshes Ready columns whose band
        // changed through the budgeted remesh path — no world recreate.
        void SetLod0Radius(int lod0Radius);
        const VoxelConfig &Config() const { return m_cfg; }
        // False once a Scene buffer rebuild (scene load, play-stop restore) wiped the shared arena
        // out from under this world — streaming into it would just warn-spam; recreate instead.
        bool IsArenaAlive() const;

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
            std::array<ArenaHandle, kSectionCount> transparentHandles{}; // water sub-mesh per section
            std::array<bool, kSectionCount> sectionUploaded{};
            std::array<bool, kSectionCount> remeshPending{};
            std::array<bool, kSectionCount> dirtyAfterRemesh{};
            std::array<uint8_t, kSectionCount> sectionLod{}; // lod each section's live mesh was built at
            std::array<uint8_t, kSectionCount> remeshLod{};  // lod each in-flight remesh was enqueued at
            uint16_t touchedSectionMask = 0;                 // sections edited or loaded from disk; drives save
            int lod = 0;                                     // lod new meshing/remeshing enqueues at
            // Coarse bands generate coarse block data (see ITerrainGenerator::Generate) — GetBlock/
            // raycasts out there are approximate. When the band drops below genLod the column
            // regenerates finer data in the background (old meshes stay live until the swap).
            uint8_t genLod = 0;        // lod the column's block data was generated at
            uint8_t regenLod = 0;      // target lod of the in-flight regeneration
            bool regenPending = false; // a finer-data regeneration is in flight (state stays Ready)
        };

        ColumnCoord m_streamAnchorColumn{};
        bool m_streamAnchorValid = false;

        static uint64_t ColumnKey(ColumnCoord coord);
        static ColumnCoord AnchorToColumn(const vec3 &worldPos);
        static int ColumnDistance(ColumnCoord a, ColumnCoord b);
        // currentLod >= 0 applies band hysteresis: the band only changes once the anchor is clearly
        // past the boundary, so hovering on a band edge can't flip-flop remeshes.
        int DesiredLod(ColumnCoord coord, int currentLod = -1) const;
        int RequestRadius() const;
        bool InWorldBounds(ColumnCoord coord) const;
        void SweepLodTransitions();
        void RemeshColumnAtLod(ColumnState &state, int lod);
        ColumnState *FindColumnState(ColumnCoord coord);
        const ColumnState *FindColumnState(ColumnCoord coord) const;
        ChunkColumn *FindColumn(ColumnCoord coord);
        const ChunkColumn *FindColumn(ColumnCoord coord) const;
        void RegisterDefaultBlocks();
        void CreateHostMesh();
        void RequestColumnsForAnchor();
        void EnqueueColumnGeneration(ColumnCoord coord);
        std::shared_future<ChunkColumn> EnqueueGenerationJob(ColumnCoord coord, int genLod);
        void ProcessGenerationResults();
        void ApplyPendingEdits(uint64_t key, ChunkColumn &column);
        void EnqueueColumnMeshing(ColumnState &state);
        bool NeighborGenerationInProgress(ColumnCoord coord) const;
        void TryStartColumnMeshing(ColumnState &state);
        void ProcessPendingMeshing();
        int ProcessReadyMeshUploads(CommandBuffer *cmd, int budget);
        // Uploads the section's opaque AND transparent (water) streams as two independent arena meshes.
        // Returns false only on OPAQUE OOM (caller stops/retries the frame); transparent OOM is non-fatal
        // (water for that section is skipped until a later pool grow). Out-handles are invalid when a
        // stream is empty.
        bool UploadSectionMesh(CommandBuffer *cmd, ColumnCoord coord, int si, const MeshData &mesh,
                               ArenaHandle &opaqueOut, ArenaHandle &transparentOut);
        void ReleaseColumn(ColumnState &state);
        void EnqueueSectionRemeshBatch(ColumnState &state, const std::vector<int> &sections);
        // applyBudget caps section uploads applied per frame so a burst of completed remeshes spreads
        // over frames instead of spiking; remaining ready futures stay pending for the next frame.
        void ProcessDirtyRemeshResults(CommandBuffer *cmd, int applyBudget);
        void RemeshDirtySections(CommandBuffer *cmd, int applyBudget);
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
        vec3 m_lodSweepAnchor = vec3(1e30f); // anchor position the last LOD band sweep ran at
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
