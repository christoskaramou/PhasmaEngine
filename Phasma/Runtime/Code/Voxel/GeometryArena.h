#pragma once
#include "Voxel/FreeListAllocator.h"
#include "Voxel/IChunkMesher.h" // MeshData, Vertex

namespace pe
{
    class Scene;
    class CommandBuffer;
    struct MeshRuntime;
} // namespace pe

// Productized Spike 0A. Streams section meshes into the Scene's SHARED geometry buffer (no
// UploadBuffers rebuild) so voxel chunks inherit the existing GBuffer / indirect / frustum / Hi-Z /
// shadow path. Vertex/index space is sub-allocated by two coalescing free lists over the reserved
// arena region; scene mesh slots are dense and relocate on swap-remove, so the arena owns the
// authoritative id->slot table and callers hold only an opaque handle.
namespace pe::voxel
{
    struct ArenaHandle
    {
        uint32_t id = 0; // 0 == invalid
        bool valid = false;
    };

    class GeometryArena
    {
    public:
        // Reserve capacity in `scene` and size the free lists. vtxCapVertices = max live vertices
        // (shared across both vertex streams); idxCapBytes = index tail bytes; maxSections = extra
        // indirect-draw slots to pad the scene's per-mesh buffers by.
        void Init(Scene *scene, uint32_t vtxCapVertices, uint32_t idxCapBytes, uint32_t maxSections);

        // Record an upload into `cmd` (no CPU stall — caller submits before the cull dispatch).
        // Verts are packed section-local; sectionOrigin becomes the slot's AABB min, which the voxel VS
        // adds back as the world origin. Every section shares one identity host node (hostDataOffset =
        // that node's transform-storage offset). Returns an invalid handle on OOM.
        ArenaHandle Upload(CommandBuffer *cmd, const MeshData &mesh, const vec3 &sectionOrigin,
                           uint32_t hostDataOffset, const MeshRuntime &runtime);

        // Queue a handle for removal. The GPU swap-remove + range free happen in Update(); freed
        // ranges are retired a few frames later so no stale two-phase-occlusion draw reuses them.
        void Release(const ArenaHandle &handle);

        // Per-frame, with the frame command buffer: retire matured ranges, then process queued
        // removals (records the swap-removes into `cmd`).
        void Update(CommandBuffer *cmd);

        // Grow the dedicated voxel buffers when a free list crosses its high-water mark, so dense
        // sections (caves/AO) never OOM into terrain holes. Records the copy into `cmd` (no GPU drain);
        // call before this frame's arena Update/uploads so the grown buffer is live for them.
        void GrowIfNeeded(CommandBuffer *cmd);

        void Destroy();

        uint32_t LiveSectionCount() const { return static_cast<uint32_t>(m_entries.size()); }
        bool IsInitialized() const { return m_scene != nullptr; }

    private:
        struct Entry
        {
            uint32_t vtxOffset = 0; // free-list offset, in VERTICES
            uint32_t vtxCount = 0;
            uint32_t idxOffset = 0; // free-list offset, in BYTES (within the arena index region)
            uint32_t idxBytes = 0;
            int sceneSlot = -1; // current Scene mesh slot (mutated by swap-remove relocation)
        };
        struct Retire
        {
            uint32_t vtxOffset = 0, vtxCount = 0, idxOffset = 0, idxBytes = 0;
            int sceneSlot = -1;      // tombstoned scene slot returned to the reuse pool at retireTick
            uint64_t retireTick = 0; // tick at which the ranges + slot become reusable
        };

        Scene *m_scene = nullptr;
        std::unique_ptr<FreeListAllocator> m_vtxAlloc; // units = vertices
        std::unique_ptr<FreeListAllocator> m_idxAlloc; // units = bytes
        uint32_t m_vertexBase = 0;
        size_t m_idxByteBase = 0;

        std::unordered_map<uint32_t, Entry> m_entries; // id -> entry
        std::unordered_map<int, uint32_t> m_slotToId;  // scene slot -> id
        std::vector<uint32_t> m_pendingRelease;        // ids queued by Release()
        std::vector<Retire> m_retire;

        uint32_t m_nextId = 1;
        uint64_t m_tick = 0;
        // Frames-in-flight + two-phase-occlusion temporal latency. A removed section's ranges must not
        // be handed to a new upload for at least this many Update() ticks, or a stale filtered draw
        // could index reused geometry.
        static constexpr uint64_t kRetireDelay = 4;
    };
} // namespace pe::voxel
