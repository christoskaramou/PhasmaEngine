#include "Voxel/GeometryArena.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h" // MeshRuntime

namespace pe::voxel
{
    void GeometryArena::Init(Scene *scene, uint32_t vtxCapVertices, uint32_t idxCapBytes, uint32_t maxSections)
    {
        m_scene = scene;
        if (!m_scene)
            return;

        // Single packed vertex stream now (8 B/vert). ReserveArenaCapacity sizes the dedicated voxel
        // vertex buffer from vtxHeadroomBytes / sizeof(VoxelVertex); the posUv headroom is unused.
        const uint32_t vtxHeadroomBytes = vtxCapVertices * static_cast<uint32_t>(sizeof(VoxelVertex));
        m_scene->ReserveArenaCapacity(vtxHeadroomBytes, idxCapBytes, 0u, maxSections);

        m_vertexBase = m_scene->GetArenaVertexBase();
        m_idxByteBase = m_scene->GetArenaIdxByteBase();

        // Free lists span the reserved region. The vertex list counts VERTICES (the shared index used
        // by both vertex streams); the index list counts BYTES.
        m_vtxAlloc = std::make_unique<FreeListAllocator>(m_scene->GetArenaVertexCapacity());
        m_idxAlloc = std::make_unique<FreeListAllocator>(static_cast<uint32_t>(m_scene->GetArenaIdxCapacity()));

        m_entries.clear();
        m_slotToId.clear();
        m_pendingRelease.clear();
        m_retire.clear();
        m_nextId = 1;
        m_tick = 0;
    }

    ArenaHandle GeometryArena::Upload(CommandBuffer *cmd, const MeshData &mesh, const vec3 &sectionOrigin,
                                      uint32_t hostDataOffset, const MeshRuntime &runtime)
    {
        ArenaHandle invalid{};
        if (!m_scene || !m_vtxAlloc || !m_idxAlloc)
            return invalid;

        const uint32_t vertCount = static_cast<uint32_t>(mesh.vertices.size());
        const uint32_t idxCount = static_cast<uint32_t>(mesh.indices.size());
        if (vertCount == 0 || idxCount == 0)
            return invalid; // empty (all-air) section — nothing to draw

        const uint32_t vtxOff = m_vtxAlloc->Alloc(vertCount);
        if (vtxOff == FreeListAllocator::kInvalid)
        {
            PE_WARN("GeometryArena::Upload: vertex arena OOM (need %u, used %u)", vertCount, m_vtxAlloc->Used());
            return invalid;
        }
        const uint32_t idxBytes = idxCount * static_cast<uint32_t>(sizeof(uint16_t));
        const uint32_t idxOff = m_idxAlloc->Alloc(idxBytes);
        if (idxOff == FreeListAllocator::kInvalid)
        {
            m_vtxAlloc->Free(vtxOff, vertCount);
            PE_WARN("GeometryArena::Upload: index arena OOM (need %u bytes, used %u)", idxBytes, m_idxAlloc->Used());
            return invalid;
        }

        const uint32_t vertexIndex = m_vertexBase + vtxOff;
        const size_t idxByteOffset = m_idxByteBase + idxOff;

        // Tight section bounds: the mesher re-bases packed positions to localMin, so aabbMin
        // (= sectionOrigin + localMin) still serves as the VS origin (world = aabbMin + local), while
        // the box hugs the real surfaces. This is what lets Hi-Z occlusion cull sparse cave/overhang
        // sections — the old full 16^3 cube's nearest corner sat in empty air and never tested occluded.
        AABB box;
        box.min = vec3(sectionOrigin.x + static_cast<float>(mesh.localMin[0]),
                       sectionOrigin.y + static_cast<float>(mesh.localMin[1]),
                       sectionOrigin.z + static_cast<float>(mesh.localMin[2]));
        box.max = vec3(sectionOrigin.x + static_cast<float>(mesh.localMax[0]),
                       sectionOrigin.y + static_cast<float>(mesh.localMax[1]),
                       sectionOrigin.z + static_cast<float>(mesh.localMax[2]));

        const int slot = m_scene->AddArenaMesh(vertexIndex, idxByteOffset, mesh.vertices, mesh.indices,
                                               box, hostDataOffset, runtime, cmd);
        if (slot < 0)
        {
            m_vtxAlloc->Free(vtxOff, vertCount);
            m_idxAlloc->Free(idxOff, idxBytes);
            PE_WARN("GeometryArena::Upload: Scene::AddArenaMesh failed");
            return invalid;
        }

        const uint32_t id = m_nextId++;
        Entry e{};
        e.vtxOffset = vtxOff;
        e.vtxCount = vertCount;
        e.idxOffset = idxOff;
        e.idxBytes = idxBytes;
        e.sceneSlot = slot;
        m_entries[id] = e;

        ArenaHandle h{};
        h.id = id;
        h.valid = true;
        return h;
    }

    void GeometryArena::Release(const ArenaHandle &handle)
    {
        if (!handle.valid || handle.id == 0)
            return;
        if (m_entries.find(handle.id) == m_entries.end())
            return; // already released
        m_pendingRelease.push_back(handle.id);
    }

    void GeometryArena::Update(CommandBuffer *cmd)
    {
        if (!m_scene || !m_vtxAlloc || !m_idxAlloc)
            return;

        ++m_tick;

        // Retire matured ranges back to the free lists AND return the tombstoned scene slot to the
        // reuse pool. Delayed so no in-flight draw (nor a stale two-phase-occlusion filtered draw) can
        // index the reused geometry, and — critically — so a slot's Mesh_Constants are overwritten by a
        // reusing AddArenaMesh only once no in-flight frame still reads them.
        if (!m_retire.empty())
        {
            std::vector<Retire> stillPending;
            stillPending.reserve(m_retire.size());
            for (const Retire &r : m_retire)
            {
                if (r.retireTick <= m_tick)
                {
                    m_vtxAlloc->Free(r.vtxOffset, r.vtxCount);
                    m_idxAlloc->Free(r.idxOffset, r.idxBytes);
                    if (r.sceneSlot >= 0)
                        m_scene->FreeArenaSlot(r.sceneSlot);
                }
                else
                {
                    stillPending.push_back(r);
                }
            }
            m_retire.swap(stillPending);
        }

        // Tombstone queued removals now (zeroes the slot's draw/visibility so it stops rendering this
        // frame); its slot + ranges become reusable only after kRetireDelay. No swap-remove relocation,
        // so no slot's constants are CPU-rewritten while an in-flight frame still reads them.
        for (uint32_t id : m_pendingRelease)
        {
            auto it = m_entries.find(id);
            if (it == m_entries.end())
                continue;
            const Entry e = it->second; // copy before erasing

            m_scene->RemoveArenaMesh(e.sceneSlot, cmd);
            m_retire.push_back(
                {e.vtxOffset, e.vtxCount, e.idxOffset, e.idxBytes, e.sceneSlot, m_tick + kRetireDelay});
            m_entries.erase(id);
        }
        m_pendingRelease.clear();
    }

    void GeometryArena::GrowIfNeeded(CommandBuffer *cmd)
    {
        if (!m_scene || !m_vtxAlloc || !m_idxAlloc || !cmd)
            return;

        const uint32_t vtxCap = m_vtxAlloc->Capacity(); // vertices
        const uint32_t idxCap = m_idxAlloc->Capacity(); // bytes
        // Grow at 80% used so one frame's worth of uploads can't spill past capacity before the next
        // check (uploadBudgetPerFrame * worst-section-verts << 20% of the pool at any sane radius).
        // ponytail: 80% + 1.5x is a fixed heuristic; tune if a frame can ever exceed the 20% headroom.
        const bool vtxPressure =
            vtxCap > 0 && static_cast<uint64_t>(m_vtxAlloc->Used()) * 100u >= static_cast<uint64_t>(vtxCap) * 80u;
        const bool idxPressure =
            idxCap > 0 && static_cast<uint64_t>(m_idxAlloc->Used()) * 100u >= static_cast<uint64_t>(idxCap) * 80u;
        if (!vtxPressure && !idxPressure)
            return;

        const uint32_t newVtxCap = vtxPressure ? vtxCap + vtxCap / 2u + 1u : vtxCap;
        const uint32_t newIdxCap = idxPressure ? idxCap + idxCap / 2u + 1u : idxCap;

        if (m_scene->GrowArenaVoxelCapacity(cmd, newVtxCap, static_cast<size_t>(newIdxCap)))
        {
            if (vtxPressure)
                m_vtxAlloc->Grow(newVtxCap);
            if (idxPressure)
                m_idxAlloc->Grow(newIdxCap);
            PE_INFO("GeometryArena: grew voxel pool (vtx cap %u->%u verts, idx cap %u->%u bytes)", vtxCap,
                    m_vtxAlloc->Capacity(), idxCap, m_idxAlloc->Capacity());
        }
    }

    void GeometryArena::Destroy()
    {
        m_entries.clear();
        m_slotToId.clear();
        m_pendingRelease.clear();
        m_retire.clear();
        m_vtxAlloc.reset();
        m_idxAlloc.reset();
        m_vertexBase = 0;
        m_idxByteBase = 0;
        m_nextId = 1;
        m_tick = 0;
        m_scene = nullptr;
    }
} // namespace pe::voxel
