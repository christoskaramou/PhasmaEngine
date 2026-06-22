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

        // ReserveArenaCapacity derives the shared vertex capacity from max(vtx/sizeof(Vertex),
        // posUv/sizeof(PositionUvVertex)); pass both headrooms as vtxCapVertices so they agree.
        const uint32_t vtxHeadroomBytes = vtxCapVertices * static_cast<uint32_t>(sizeof(Vertex));
        const uint32_t posUvHeadroomBytes = vtxCapVertices * static_cast<uint32_t>(sizeof(PositionUvVertex));
        m_scene->ReserveArenaCapacity(vtxHeadroomBytes, idxCapBytes, posUvHeadroomBytes, maxSections);

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
        const uint32_t idxBytes = idxCount * static_cast<uint32_t>(sizeof(uint32_t));
        const uint32_t idxOff = m_idxAlloc->Alloc(idxBytes);
        if (idxOff == FreeListAllocator::kInvalid)
        {
            m_vtxAlloc->Free(vtxOff, vertCount);
            PE_WARN("GeometryArena::Upload: index arena OOM (need %u bytes, used %u)", idxBytes, m_idxAlloc->Used());
            return invalid;
        }

        const uint32_t vertexIndex = m_vertexBase + vtxOff;
        const size_t idxByteOffset = m_idxByteBase + idxOff;

        // Bake the section origin into vertex positions (all sections share one identity host node) and
        // derive the depth/shadow PositionUvVertex stream + a world-space AABB for culling.
        std::vector<Vertex> baked = mesh.vertices;
        std::vector<PositionUvVertex> posUv(vertCount); // value-initialized -> joints/weights zeroed

        vec3 mn(baked[0].position[0] + sectionOrigin.x,
                baked[0].position[1] + sectionOrigin.y,
                baked[0].position[2] + sectionOrigin.z);
        vec3 mx = mn;
        for (uint32_t i = 0; i < vertCount; ++i)
        {
            Vertex &v = baked[i];
            v.position[0] += sectionOrigin.x;
            v.position[1] += sectionOrigin.y;
            v.position[2] += sectionOrigin.z;

            posUv[i].position[0] = v.position[0];
            posUv[i].position[1] = v.position[1];
            posUv[i].position[2] = v.position[2];
            posUv[i].uv[0] = v.uv[0];
            posUv[i].uv[1] = v.uv[1];

            mn.x = v.position[0] < mn.x ? v.position[0] : mn.x;
            mn.y = v.position[1] < mn.y ? v.position[1] : mn.y;
            mn.z = v.position[2] < mn.z ? v.position[2] : mn.z;
            mx.x = v.position[0] > mx.x ? v.position[0] : mx.x;
            mx.y = v.position[1] > mx.y ? v.position[1] : mx.y;
            mx.z = v.position[2] > mx.z ? v.position[2] : mx.z;
        }
        AABB box;
        box.min = mn;
        box.max = mx;

        const int slot = m_scene->AddArenaMesh(vertexIndex, idxByteOffset, baked, posUv, mesh.indices,
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
        m_slotToId[slot] = id;

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

        // Retire matured ranges back to the free lists (delayed so no in-flight occlusion draw can
        // index reused geometry).
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
                }
                else
                {
                    stillPending.push_back(r);
                }
            }
            m_retire.swap(stillPending);
        }

        // Process queued removals. Each swap-remove may relocate the last arena slot into the freed
        // one; the arena's id<->slot maps are fixed using the relocated slot the Scene returns.
        for (uint32_t id : m_pendingRelease)
        {
            auto it = m_entries.find(id);
            if (it == m_entries.end())
                continue;
            const Entry e = it->second; // copy before erasing

            const int relocated = m_scene->RemoveArenaMesh(e.sceneSlot, cmd);
            if (relocated >= 0)
            {
                // The mesh at slot `relocated` (old last slot) now lives at e.sceneSlot.
                auto movedIt = m_slotToId.find(relocated);
                if (movedIt != m_slotToId.end())
                {
                    const uint32_t movedId = movedIt->second;
                    auto movedEntry = m_entries.find(movedId);
                    if (movedEntry != m_entries.end())
                        movedEntry->second.sceneSlot = e.sceneSlot;
                    m_slotToId[e.sceneSlot] = movedId;
                    m_slotToId.erase(relocated);
                }
            }
            else
            {
                m_slotToId.erase(e.sceneSlot);
            }

            m_retire.push_back({e.vtxOffset, e.vtxCount, e.idxOffset, e.idxBytes, m_tick + kRetireDelay});
            m_entries.erase(id);
        }
        m_pendingRelease.clear();
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
