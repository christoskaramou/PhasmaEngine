#include "Voxel/VoxelWorld.h"
#include "Voxel/GreedyMesher.h"
#include "Voxel/ColumnChunkStore.h"
#include "Voxel/VoxelCollider.h"
#include "Voxel/VoxelMaterial.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Scene/Material.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"
#include "Voxel/NoiseGen.h"

namespace pe::voxel
{
    namespace
    {
        constexpr BlockId kStoneBlock = 1;
        constexpr BlockId kDirtBlock = 2;
        constexpr BlockId kGrassBlock = 3;
        constexpr BlockId kWaterBlock = 4;
        constexpr size_t kInvalidNodeDataOffset = static_cast<size_t>(-1);
        constexpr size_t kMaxHostDataOffset = 0xFFFFFFFFull;
        constexpr size_t kMaxPendingUpdateCommands = 4;

        template <typename T>
        bool FutureReady(const std::shared_future<T> &future)
        {
            return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }

        int ClampInt(int v, int lo, int hi)
        {
            return std::max(lo, std::min(v, hi));
        }

        // Horizontal neighbor snapshots for seam-aware meshing (thread-safe copies).
        BlockId SampleSectionBlock(const ChunkColumn &center, const ColumnNeighbors &neighbors, ColumnCoord coord,
                                   int si, int lx, int ly, int lz)
        {
            const int worldY = si * kSectionDim + ly;
            if (worldY < 0 || worldY >= kWorldHeight)
                return kAir;

            int cxOff = 0;
            int czOff = 0;
            if (lx < 0)
                cxOff = -1;
            else if (lx >= kSectionDim)
                cxOff = 1;
            if (lz < 0)
                czOff = -1;
            else if (lz >= kSectionDim)
                czOff = 1;

            int sampleLx = lx;
            int sampleLz = lz;
            if (cxOff == -1)
                sampleLx = lx + kSectionDim;
            else if (cxOff == 1)
                sampleLx = lx - kSectionDim;
            if (czOff == -1)
                sampleLz = lz + kSectionDim;
            else if (czOff == 1)
                sampleLz = lz - kSectionDim;

            const ChunkColumn *col = nullptr;
            if (cxOff == 0 && czOff == 0)
                col = &center;
            else if (cxOff == -1 && czOff == 0)
                col = neighbors.negX ? neighbors.negX.get() : nullptr;
            else if (cxOff == 1 && czOff == 0)
                col = neighbors.posX ? neighbors.posX.get() : nullptr;
            else if (cxOff == 0 && czOff == -1)
                col = neighbors.negZ ? neighbors.negZ.get() : nullptr;
            else if (cxOff == 0 && czOff == 1)
                col = neighbors.posZ ? neighbors.posZ.get() : nullptr;
            else if (cxOff == -1 && czOff == -1)
                col = neighbors.negXnegZ ? neighbors.negXnegZ.get() : nullptr;
            else if (cxOff == -1 && czOff == 1)
                col = neighbors.negXposZ ? neighbors.negXposZ.get() : nullptr;
            else if (cxOff == 1 && czOff == -1)
                col = neighbors.posXnegZ ? neighbors.posXnegZ.get() : nullptr;
            else if (cxOff == 1 && czOff == 1)
                col = neighbors.posXposZ ? neighbors.posXposZ.get() : nullptr;

            if (!col || sampleLx < 0 || sampleLx >= kSectionDim || sampleLz < 0 || sampleLz >= kSectionDim)
                return kAir;

            (void)coord;
            return col->GetLocal(sampleLx, worldY, sampleLz);
        }

        struct SectionSampleCtx
        {
            const ChunkColumn *column = nullptr;
            const ColumnNeighbors *neighbors = nullptr;
            ColumnCoord coord{};
            int si = 0;
        };

        BlockId SectionSampleThunk(void *ctx, int lx, int ly, int lz)
        {
            const SectionSampleCtx *c = static_cast<const SectionSampleCtx *>(ctx);
            return SampleSectionBlock(*c->column, *c->neighbors, c->coord, c->si, lx, ly, lz);
        }

        // Highest lod the streamer requests: 4-block cells. Coarser reads badly even at the horizon,
        // and the mesher's 5-bit packed positions allow it without format changes.
        constexpr int kMaxVoxelLod = 2;

        MeshData MeshSectionCpu(const ChunkColumn &column, const BlockRegistry &registry, int si,
                                ColumnCoord coord, const ColumnNeighbors &neighbors, int lod)
        {
            GreedyMesher mesher;
            SectionSampleCtx ctx{&column, &neighbors, coord, si};
            return mesher.Mesh(SectionSampleThunk, &ctx, registry, lod);
        }

        // True when section si has opaque blocks within two voxels of the horizontal face that
        // borders the neighbor at (dcx, dcz) — covers emitted faces and AO samples at the seam.
        bool SectionNeedsCardinalSeam(const ChunkColumn &col, int si, int dcx, int dcz,
                                      const BlockRegistry &reg)
        {
            const ChunkSection &sec = col.Section(si);
            auto opaqueAt = [&](int lx, int ly, int lz) -> bool
            {
                const BlockId id = sec.Get(lx, ly, lz);
                return id != kAir && reg.IsOpaque(id);
            };

            if (dcx == -1)
            {
                for (int ly = 0; ly < kSectionDim; ++ly)
                    for (int lz = 0; lz < kSectionDim; ++lz)
                        if (opaqueAt(14, ly, lz) || opaqueAt(15, ly, lz))
                            return true;
            }
            else if (dcx == 1)
            {
                for (int ly = 0; ly < kSectionDim; ++ly)
                    for (int lz = 0; lz < kSectionDim; ++lz)
                        if (opaqueAt(0, ly, lz) || opaqueAt(1, ly, lz))
                            return true;
            }
            else if (dcz == -1)
            {
                for (int ly = 0; ly < kSectionDim; ++ly)
                    for (int lx = 0; lx < kSectionDim; ++lx)
                        if (opaqueAt(lx, ly, 14) || opaqueAt(lx, ly, 15))
                            return true;
            }
            else if (dcz == 1)
            {
                for (int ly = 0; ly < kSectionDim; ++ly)
                    for (int lx = 0; lx < kSectionDim; ++lx)
                        if (opaqueAt(lx, ly, 0) || opaqueAt(lx, ly, 1))
                            return true;
            }
            return false;
        }

        // True when section si has opaque blocks in the 2×2 corner patch that meets the diagonal
        // neighbor at (dcx, dcz).
        bool SectionNeedsDiagonalSeam(const ChunkColumn &col, int si, int dcx, int dcz,
                                      const BlockRegistry &reg)
        {
            const int lx0 = (dcx == -1) ? 14 : 0;
            const int lx1 = (dcx == -1) ? 15 : 1;
            const int lz0 = (dcz == -1) ? 14 : 0;
            const int lz1 = (dcz == -1) ? 15 : 1;
            const ChunkSection &sec = col.Section(si);
            for (int ly = 0; ly < kSectionDim; ++ly)
            {
                for (int lx = lx0; lx <= lx1; ++lx)
                {
                    for (int lz = lz0; lz <= lz1; ++lz)
                    {
                        const BlockId id = sec.Get(lx, ly, lz);
                        if (id != kAir && reg.IsOpaque(id))
                            return true;
                    }
                }
            }
            return false;
        }

        Vertex MakeHostVertex()
        {
            Vertex v{};
            v.normals[1] = 1.0f;
            v.tangent[0] = 1.0f;
            v.tangent[3] = 1.0f;
            v.color[0] = 1.0f;
            v.color[1] = 1.0f;
            v.color[2] = 1.0f;
            v.color[3] = 1.0f;
            return v;
        }

        PositionUvVertex MakeHostPositionUv()
        {
            PositionUvVertex v{};
            return v;
        }
    } // namespace

    VoxelWorld::VoxelWorld() = default;

    VoxelWorld::~VoxelWorld()
    {
        Destroy();
    }

    uint64_t VoxelWorld::ColumnKey(ColumnCoord coord)
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(coord.cx)) << 32u) |
               static_cast<uint32_t>(coord.cz);
    }

    ColumnCoord VoxelWorld::AnchorToColumn(const vec3 &worldPos)
    {
        return WorldToColumn(static_cast<int>(std::floor(worldPos.x)),
                             static_cast<int>(std::floor(worldPos.z)));
    }

    int VoxelWorld::ColumnDistance(ColumnCoord a, ColumnCoord b)
    {
        return std::max(std::abs(a.cx - b.cx), std::abs(a.cz - b.cz));
    }

    int VoxelWorld::DesiredLod(ColumnCoord coord) const
    {
        if (m_cfg.lod0Radius <= 0 || !m_streamAnchorValid)
            return 0;
        return std::min(kMaxVoxelLod, ColumnDistance(coord, m_streamAnchorColumn) / m_cfg.lod0Radius);
    }

    void VoxelWorld::RemeshColumnAtLod(ColumnState &state, int lod)
    {
        state.lod = lod;
        std::vector<int> allSections(kSectionCount);
        for (int si = 0; si < kSectionCount; ++si)
            allSections[si] = si;
        EnqueueSectionRemeshBatch(state, allSections);
    }

    VoxelWorld::ColumnState *VoxelWorld::FindColumnState(ColumnCoord coord)
    {
        auto it = m_columns.find(ColumnKey(coord));
        return it == m_columns.end() ? nullptr : &it->second;
    }

    const VoxelWorld::ColumnState *VoxelWorld::FindColumnState(ColumnCoord coord) const
    {
        auto it = m_columns.find(ColumnKey(coord));
        return it == m_columns.end() ? nullptr : &it->second;
    }

    ChunkColumn *VoxelWorld::FindColumn(ColumnCoord coord)
    {
        ColumnState *state = FindColumnState(coord);
        if (!state || state->state == ColumnLoadState::Generating || state->state == ColumnLoadState::Unloading)
            return nullptr;
        return state->column.get();
    }

    const ChunkColumn *VoxelWorld::FindColumn(ColumnCoord coord) const
    {
        const ColumnState *state = FindColumnState(coord);
        if (!state || state->state == ColumnLoadState::Generating || state->state == ColumnLoadState::Unloading)
            return nullptr;
        return state->column.get();
    }

    void VoxelWorld::Create(Scene *scene, const VoxelConfig &cfg)
    {
        Destroy();

        PE_ERROR_IF(!scene, "VoxelWorld::Create requires a valid Scene");
        if (!scene)
            return;

        m_scene = scene;
        m_cfg = cfg;
        m_cfg.loadRadius = std::max(0, m_cfg.loadRadius);
        m_cfg.unloadMargin = std::max(0, m_cfg.unloadMargin);
        m_cfg.uploadBudgetPerFrame = std::max(1, m_cfg.uploadBudgetPerFrame);
        m_cfg.groundY = ClampInt(m_cfg.groundY, 0, kWorldHeight);
        m_cfg.lod0Radius = std::max(0, m_cfg.lod0Radius);
        m_saveRoot = ColumnChunkStore::ResolveRoot(m_cfg.saveDir);
        if (!m_saveRoot.empty())
            PE_INFO("VoxelWorld: column persistence root %s", m_saveRoot.generic_string().c_str());
#if defined(PE_DEBUG)
        // Greedy mesh + noise worldgen are unoptimized enough in Debug that huge radii stall for
        // tens of seconds. Release is unaffected; scripts can still request any radius there.
        constexpr int kDebugMaxLoadRadius = 12;
        if (m_cfg.loadRadius > kDebugMaxLoadRadius)
        {
            PE_INFO("VoxelWorld: clamping load_radius %d -> %d (Debug build)", m_cfg.loadRadius,
                    kDebugMaxLoadRadius);
            m_cfg.loadRadius = kDebugMaxLoadRadius;
        }
#endif
        m_anchor = vec3(0.0f);

        // Engine ships a default noise generator; a game keeps its own by calling
        // SetTerrainGenerator before Create (m_generatorOverridden guards it from this default).
        if (!m_generatorOverridden)
            m_generator = std::make_shared<NoiseGen>(m_cfg.groundY);

        RegisterDefaultBlocks();
        CreateHostMesh();

        m_scene->FlushPendingGpuWork();

        // Build the voxel atlas (upload + Scene::UpdateTextures) BEFORE reserving arena
        // capacity. UpdateTextures rebuilds m_meshConstants/materialTable to the NON-arena size; if it
        // ran after arena.Init it would shrink the arena's reservation and overflow the per-section
        // mesh-constants write (Buffer::CopyDataRaw range overflow). Arena reservation must be the LAST
        // buffer-sizing op before streaming starts.
        m_voxelMaterial = std::make_unique<VoxelMaterial>();
        m_voxelMaterial->Build(m_scene, {Path::RuntimeAssets + "Textures/Voxel/grass.png",
                                         Path::RuntimeAssets + "Textures/Voxel/dirt.png",
                                         Path::RuntimeAssets + "Textures/Voxel/stone.png",
                                         Path::RuntimeAssets + "Textures/Voxel/water.png"});
        if (m_voxelMaterial->Atlas() && m_voxelMaterial->Atlas()->GetSRV())
            m_scene->SetVoxelAtlasView(m_voxelMaterial->Atlas()->GetSRV());

        const size_t hostDataOffset = m_scene->GetNodeDataOffset(m_hostNode);
        PE_ERROR_IF(hostDataOffset == kInvalidNodeDataOffset, "VoxelWorld host node has no drawable data offset");
        PE_ERROR_IF(hostDataOffset > kMaxHostDataOffset, "VoxelWorld host data offset exceeds Mesh_Constants storage");
        m_hostDataOffset = static_cast<uint32_t>(hostDataOffset);

        m_materialGpuIndex = m_hostMaterial ? m_hostMaterial->gpuIndex : 0xFFFFFFFF;
        PE_ERROR_IF(m_materialGpuIndex == 0xFFFFFFFF, "VoxelWorld host material was not assigned a GPU index");

        const int capacityRadius = m_cfg.loadRadius + m_cfg.unloadMargin;
        const int gridDim = capacityRadius * 2 + 1;
        const uint32_t gridSections = static_cast<uint32_t>(gridDim * gridDim * kSectionCount);
        // Initial per-section budget for the dedicated voxel pool. The arena GROWS on pressure
        // (GeometryArena::GrowIfNeeded, 80% -> 1.5x) and OOM'd sections retry after the grow instead of
        // dropping geometry, so this only sizes the initial reservation. Full-detail surface+AO sections
        // (no LOD) run ~1k verts; 1024 fits a normal streaming front without grow churn on first fill.
        // Packed verts are 8 B (VoxelVertex), so a loadRadius-8 grid starts at ~tens of MB.
        const uint32_t kVertsPerSection = 1024u;
        const uint32_t kIndicesPerSection = 1536u;
        // With LOD on, only the lod-0 core needs the full per-section budget; coarse rings mesh at
        // 1/4 or less. Estimating them at 1/4 keeps big-radius worlds from reserving hundreds of MB
        // up front — GrowIfNeeded absorbs any underestimate.
        uint32_t fullSections = gridSections;
        uint32_t coarseSections = 0;
        if (m_cfg.lod0Radius > 0 && m_cfg.lod0Radius < capacityRadius)
        {
            const int lod0Dim = m_cfg.lod0Radius * 2 + 1;
            fullSections = static_cast<uint32_t>(lod0Dim * lod0Dim * kSectionCount);
            coarseSections = gridSections - fullSections;
        }
        const uint32_t vtxCapVertices = fullSections * kVertsPerSection + coarseSections * (kVertsPerSection / 4);
        const uint32_t idxCapBytes = (fullSections * kIndicesPerSection + coarseSections * (kIndicesPerSection / 4)) *
                                     static_cast<uint32_t>(sizeof(uint16_t));

        m_arena.Init(m_scene, vtxCapVertices, idxCapBytes, gridSections);
        SetAnchor(m_anchor);
    }

    void VoxelWorld::Destroy()
    {
        RetireSubmittedUpdateCommands(true);
        PersistAllTouchedColumns();

        if (m_scene)
            m_scene->SetVoxelAtlasView(nullptr);
        m_voxelMaterial.reset();

        if (m_scene && m_arena.IsInitialized() && m_scene->HasArenaVoxels())
        {
            Queue *queue = RHII.GetMainQueue();
            if (queue)
            {
                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                cmd->Begin();
                for (auto &entry : m_columns)
                    ReleaseColumn(entry.second);
                m_arena.Update(cmd);
                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                cmd->Return();
            }
        }

        m_arena.Destroy();
        m_dirtySections.clear();
        m_pendingEdits.clear();
        m_columns.clear();

        if (m_scene && m_hostMeshIndex >= 0 && m_scene->IsValidMeshIndex(m_hostMeshIndex))
            m_scene->GetMesh(m_hostMeshIndex).material = nullptr;

        if (m_scene && m_hostNode && m_scene->IsNodeAlive(m_hostNode))
            m_scene->DeleteNode(m_hostNode);

        m_hostMaterial.reset();
        m_hostNode = nullptr;
        m_hostMeshIndex = -1;
        m_hostDataOffset = 0xFFFFFFFF;
        m_materialGpuIndex = 0xFFFFFFFF;
        m_saveRoot.clear();
        m_scene = nullptr;
        m_streamAnchorValid = false;
    }

    void VoxelWorld::SetAnchor(const vec3 &worldPos)
    {
        m_anchor = worldPos;
        if (!m_scene || !m_arena.IsInitialized())
            return;

        const ColumnCoord anchorCol = AnchorToColumn(worldPos);
        if (m_streamAnchorValid && anchorCol.cx == m_streamAnchorColumn.cx && anchorCol.cz == m_streamAnchorColumn.cz)
            return;

        m_streamAnchorColumn = anchorCol;
        m_streamAnchorValid = true;
        RequestColumnsForAnchor();
    }

    BlockId VoxelWorld::GetBlock(int x, int y, int z) const
    {
        const ColumnCoord coord = WorldToColumn(x, z);
        const ChunkColumn *column = FindColumn(coord);
        if (!column)
            return kAir;

        return column->GetLocal(LocalX(x), y, LocalZ(z));
    }

    void VoxelWorld::SetBlock(int x, int y, int z, BlockId id)
    {
        if (y < 0 || y >= kWorldHeight)
            return;

        const ColumnCoord coord = WorldToColumn(x, z);
        const uint64_t key = ColumnKey(coord);
        auto colIt = m_columns.find(key);
        if (colIt == m_columns.end())
            return;

        ColumnState &state = colIt->second;
        if (state.state == ColumnLoadState::Generating)
        {
            m_pendingEdits[key].push_back({x, y, z, id});
            return;
        }
        if (state.state == ColumnLoadState::Unloading || !state.column)
            return;

        if (state.column->GetLocal(LocalX(x), y, LocalZ(z)) == id)
            return; // no change

        state.column->SetLocal(LocalX(x), y, LocalZ(z), id);
        TouchSection(coord, SectionIndex(y));
        MarkEditDirtySections(coord, x, y, z);
    }

    bool VoxelWorld::Raycast(const vec3 &o, const vec3 &d, float maxDist,
                             BlockPos &hit, BlockPos &adjacent, vec3 &normal) const
    {
        const VoxelRayHit voxelHit = RaycastVoxels(o, d, maxDist, [this](int x, int y, int z)
                                                   {
                                                       const BlockId id = GetBlock(x, y, z);
                                                       return id < m_registry.Count() && m_registry.IsSolid(id); });

        hit = voxelHit.cell;
        adjacent = voxelHit.adjacent;
        normal = voxelHit.normal;
        return voxelHit.hit;
    }

    void VoxelWorld::Update()
    {
        if (!m_scene || !m_arena.IsInitialized())
            return;

        RetireSubmittedUpdateCommands(false);
        ProcessGenerationResults();
        ProcessPendingMeshing();

        Queue *queue = RHII.GetMainQueue();
        if (!queue)
            return;

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        // Grow the voxel pool before this frame's removals/uploads so the grown buffer is live for them.
        // Records the live-geometry copy into cmd (no GPU drain); barriers it ahead of the uploads, which
        // may reuse holes inside the copied region. Keeps dense cave/AO sections from OOMing into holes.
        m_arena.GrowIfNeeded(cmd);
        m_arena.Update(cmd);
        ProcessReadyMeshUploads(cmd, m_cfg.uploadBudgetPerFrame);
        // Remesh applies get their own equal per-frame allowance (separate from streaming uploads so
        // edits never starve while new terrain streams in). Bounds the previously-unbounded remesh burst.
        RemeshDirtySections(cmd, m_cfg.uploadBudgetPerFrame);
        m_scene->FlushArenaBarriers(cmd);
        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        m_submittedUpdateCmds.push_back(cmd);
    }

    BlockRegistry &VoxelWorld::Registry()
    {
        return m_registry;
    }

    void VoxelWorld::RegisterDefaultBlocks()
    {
        m_registry = BlockRegistry();
        // faceTiles order: +X,-X,+Y,-Y,+Z,-Z. Atlas layers (VoxelMaterial::Build order): 0=grass, 1=dirt, 2=stone.
        m_registry.Register({kStoneBlock, "stone", true, true, VoxelRenderClass::Opaque, {2, 2, 2, 2, 2, 2}});
        m_registry.Register({kDirtBlock, "dirt", true, true, VoxelRenderClass::Opaque, {1, 1, 1, 1, 1, 1}});
        // grass: green top (+Y -> layer 0), dirt on the 4 sides + bottom.
        m_registry.Register({kGrassBlock, "grass", true, true, VoxelRenderClass::Opaque, {1, 1, 0, 1, 1, 1}});
        // water: alpha-blended, non-solid (walk/fall through for now), atlas layer 3 on every face.
        m_registry.Register({kWaterBlock, "water", false, false, VoxelRenderClass::Transparent, {3, 3, 3, 3, 3, 3}});
    }

    void VoxelWorld::CreateHostMesh()
    {
        m_hostMaterial = std::make_unique<Material>();
        m_hostMaterial->name = "VoxelWorldHostMaterial";
        m_hostMaterial->baseColorFactor = vec4(1.0f);
        m_hostMaterial->metallic = 0.0f;
        m_hostMaterial->roughness = 1.0f;
        m_hostMaterial->occlusionStrength = 1.0f;
        m_hostMaterial->textureMask = 0u;
        m_hostMaterial->renderType = RenderType::Opaque;
        m_hostMaterial->SyncParamsFromLegacy();

        std::vector<Vertex> &vertices = m_scene->GetVertexStore();
        std::vector<PositionUvVertex> &positionUvs = m_scene->GetPositionUvStore();
        std::vector<AabbVertex> &aabbVertices = m_scene->GetAabbVertexStore();
        std::vector<uint32_t> &indices = m_scene->GetIndexStore();

        const uint32_t vertexBase = static_cast<uint32_t>(vertices.size());
        const uint32_t positionBase = static_cast<uint32_t>(positionUvs.size());
        const size_t aabbBase = aabbVertices.size();
        const uint32_t indexBase = static_cast<uint32_t>(indices.size());

        vertices.push_back(MakeHostVertex());
        vertices.push_back(MakeHostVertex());
        vertices.push_back(MakeHostVertex());

        positionUvs.push_back(MakeHostPositionUv());
        positionUvs.push_back(MakeHostPositionUv());
        positionUvs.push_back(MakeHostPositionUv());

        for (int i = 0; i < 8; ++i)
            aabbVertices.push_back({});

        indices.push_back(0u);
        indices.push_back(1u);
        indices.push_back(2u);

        Mesh mesh{};
        mesh.vertexOffset = vertexBase;
        mesh.vertexCount = 3u;
        mesh.indexOffset = indexBase;
        mesh.indexCount = 3u;
        mesh.positionsOffset = positionBase;
        mesh.aabbVertexOffset = aabbBase;
        mesh.aabbColor = 0xFFFFFFFF;
        mesh.boundingBox = {vec3(0.0f), vec3(0.0f)};
        mesh.renderType = RenderType::Opaque;
        mesh.material = m_hostMaterial.get();

        m_hostMeshIndex = m_scene->AddMesh(std::move(mesh));
        m_hostNode = m_scene->CreateNode("VoxelWorldHost");
        m_scene->SetLocalMatrix(m_hostNode, mat4(1.0f), false);
        m_scene->SetMeshRef(m_hostNode, m_hostMeshIndex);
        m_scene->SetGeometryDirty();
    }

    void VoxelWorld::RequestColumnsForAnchor()
    {
        const ColumnCoord anchorCoord = AnchorToColumn(m_anchor);
        const int radius = m_cfg.loadRadius;
        std::vector<ColumnCoord> desired;
        desired.reserve(static_cast<size_t>((radius * 2 + 1) * (radius * 2 + 1)));

        for (int dz = -radius; dz <= radius; ++dz)
            for (int dx = -radius; dx <= radius; ++dx)
                desired.push_back({anchorCoord.cx + dx, anchorCoord.cz + dz});

        std::sort(desired.begin(), desired.end(), [anchorCoord](ColumnCoord a, ColumnCoord b)
                  {
                      const int da = ColumnDistance(a, anchorCoord);
                      const int db = ColumnDistance(b, anchorCoord);
                      if (da != db)
                          return da < db;
                      if (a.cz != b.cz)
                          return a.cz < b.cz;
                      return a.cx < b.cx; });

        for (ColumnCoord coord : desired)
        {
            const uint64_t key = ColumnKey(coord);
            if (m_columns.find(key) == m_columns.end())
                EnqueueColumnGeneration(coord);
        }

        const int unloadRadius = m_cfg.loadRadius + m_cfg.unloadMargin;
        for (auto it = m_columns.begin(); it != m_columns.end();)
        {
            if (ColumnDistance(it->second.coord, anchorCoord) > unloadRadius)
            {
                const uint64_t key = it->first;
                ReleaseColumn(it->second);
                m_pendingEdits.erase(key);
                m_dirtySections.erase(std::remove_if(m_dirtySections.begin(), m_dirtySections.end(),
                                                     [key](const std::pair<uint64_t, int> &dirty)
                                                     { return dirty.first == key; }),
                                      m_dirtySections.end());
                it = m_columns.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // LOD transitions: Ready columns whose distance band changed remesh at the new lod through
        // the budgeted remesh path (columns still meshing re-check when they become Ready).
        if (m_cfg.lod0Radius > 0)
        {
            for (auto &entry : m_columns)
            {
                ColumnState &state = entry.second;
                if (state.state != ColumnLoadState::Ready || !state.column)
                    continue;
                const int lod = DesiredLod(state.coord);
                if (lod != state.lod)
                    RemeshColumnAtLod(state, lod);
            }
        }
    }

    void VoxelWorld::SetTerrainGenerator(std::shared_ptr<ITerrainGenerator> generator)
    {
        m_generator = std::move(generator);
        m_generatorOverridden = (m_generator != nullptr);
    }

    void VoxelWorld::EnqueueColumnGeneration(ColumnCoord coord)
    {
        ColumnState state{};
        state.coord = coord;
        state.state = ColumnLoadState::Generating;
        // Capture the generator by shared_ptr (not a raw VoxelWorld pointer): Destroy() drops the
        // generationFuture without waiting, so an in-flight job may outlive Destroy — the shared_ptr
        // keeps the generator alive until that last job returns. Generators must be thread-safe
        // (NoiseGen is stateless); workers run Generate() concurrently on distinct columns.
        std::shared_ptr<ITerrainGenerator> gen = m_generator;
        const std::filesystem::path saveRoot = m_saveRoot;
        state.generationFuture = ThreadPool::General.Enqueue(
            [coord, gen, saveRoot]() -> ChunkColumn
            {
                ChunkColumn column(coord);
                if (gen)
                    gen->Generate(column);
                if (!saveRoot.empty())
                    ColumnChunkStore::TryOverlay(saveRoot, column);
                return column;
            });
        m_columns.emplace(ColumnKey(coord), std::move(state));
    }

    void VoxelWorld::ProcessGenerationResults()
    {
        std::vector<uint64_t> readyColumns;
        for (auto &entry : m_columns)
        {
            ColumnState &state = entry.second;
            if (state.state == ColumnLoadState::Generating && FutureReady(state.generationFuture))
                readyColumns.push_back(entry.first);
        }

        for (uint64_t key : readyColumns)
        {
            auto it = m_columns.find(key);
            if (it == m_columns.end())
                continue;

            ColumnState &state = it->second;
            if (state.state != ColumnLoadState::Generating || !state.generationFuture.valid())
                continue;

            state.column = std::make_unique<ChunkColumn>(state.generationFuture.get());
            state.generationFuture = std::shared_future<ChunkColumn>();
            if (!m_saveRoot.empty())
                state.touchedSectionMask |= ColumnChunkStore::PersistedSectionMask(m_saveRoot, state.coord);
            ApplyPendingEdits(key, *state.column);
            state.state = ColumnLoadState::Generated;
            TryStartColumnMeshing(state);
        }
    }

    bool VoxelWorld::NeighborGenerationInProgress(ColumnCoord coord) const
    {
        static constexpr int kDirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        for (const auto &d : kDirs)
        {
            const ColumnState *st = FindColumnState({coord.cx + d[0], coord.cz + d[1]});
            if (st && st->state == ColumnLoadState::Generating)
                return true;
        }
        return false;
    }

    void VoxelWorld::TryStartColumnMeshing(ColumnState &state)
    {
        if (state.state != ColumnLoadState::Generated || !state.column)
            return;
        // Coarse columns never read neighbor blocks (the mesher caps their walls), so they skip both
        // the neighbor-generation wait and the 9-column snapshot copy — distant rings fill faster.
        const int lod = DesiredLod(state.coord);
        if (lod == 0 && NeighborGenerationInProgress(state.coord))
            return;
        state.lod = lod;
        EnqueueColumnMeshing(state);
    }

    void VoxelWorld::ProcessPendingMeshing()
    {
        for (auto &entry : m_columns)
        {
            if (entry.second.state == ColumnLoadState::Generated)
                TryStartColumnMeshing(entry.second);
        }
    }

    void VoxelWorld::ApplyPendingEdits(uint64_t key, ChunkColumn &column)
    {
        auto editsIt = m_pendingEdits.find(key);
        if (editsIt == m_pendingEdits.end())
            return;

        const ColumnCoord coord = column.Coord();
        for (const PendingEdit &edit : editsIt->second)
        {
            if (edit.y >= 0 && edit.y < kWorldHeight)
            {
                column.SetLocal(LocalX(edit.x), edit.y, LocalZ(edit.z), edit.id);
                TouchSection(coord, SectionIndex(edit.y));
            }
        }
        m_pendingEdits.erase(editsIt);
    }

    void VoxelWorld::EnqueueColumnMeshing(ColumnState &state)
    {
        if (!state.column)
            return;

        const ColumnCoord coord = state.coord;
        const int lod = state.lod;
        auto columnSnapshot = std::make_shared<ChunkColumn>(*state.column);
        // lod > 0 caps section walls instead of reading across columns, so the (expensive, ~9-column
        // deep-copy) neighbor snapshot is only taken for full-detail meshing.
        auto neighbors = std::make_shared<ColumnNeighbors>(lod == 0 ? GatherNeighborSnapshots(coord)
                                                                    : ColumnNeighbors{});
        auto registrySnapshot = std::make_shared<BlockRegistry>(m_registry);
        for (int si = 0; si < kSectionCount; ++si)
        {
            state.sectionUploaded[si] = false;
            state.meshFutures[si] = ThreadPool::General.Enqueue(
                [columnSnapshot, neighbors, registrySnapshot, coord, si, lod]() -> MeshData
                { return MeshSectionCpu(*columnSnapshot, *registrySnapshot, si, coord, *neighbors, lod); });
        }
        state.state = ColumnLoadState::Meshing;
    }

    ColumnNeighbors VoxelWorld::GatherNeighborSnapshots(ColumnCoord coord) const
    {
        ColumnNeighbors neighbors;
        auto trySnap = [&](int dcx, int dcz, std::shared_ptr<const ChunkColumn> &out)
        {
            const ColumnState *st = FindColumnState({coord.cx + dcx, coord.cz + dcz});
            if (!st || !st->column)
                return;
            if (st->state == ColumnLoadState::Generating || st->state == ColumnLoadState::Unloading ||
                st->state == ColumnLoadState::Empty)
                return;
            out = std::make_shared<ChunkColumn>(*st->column);
        };
        trySnap(-1, 0, neighbors.negX);
        trySnap(1, 0, neighbors.posX);
        trySnap(0, -1, neighbors.negZ);
        trySnap(0, 1, neighbors.posZ);
        trySnap(-1, -1, neighbors.negXnegZ);
        trySnap(-1, 1, neighbors.negXposZ);
        trySnap(1, -1, neighbors.posXnegZ);
        trySnap(1, 1, neighbors.posXposZ);
        return neighbors;
    }

    void VoxelWorld::RemeshNeighborSeams(ColumnCoord coord)
    {
        // Coarse neighbors don't sample across columns, so a fresh column can't change their meshes.
        auto remeshCardinal = [&](int dcx, int dcz)
        {
            ColumnState *st = FindColumnState({coord.cx + dcx, coord.cz + dcz});
            if (!st || st->state != ColumnLoadState::Ready || !st->column || st->lod > 0)
                return;
            std::vector<int> sections;
            for (int si = 0; si < kSectionCount; ++si)
                if (SectionNeedsCardinalSeam(*st->column, si, dcx, dcz, m_registry))
                    sections.push_back(si);
            EnqueueSectionRemeshBatch(*st, sections);
        };
        auto remeshDiagonal = [&](int dcx, int dcz)
        {
            ColumnState *st = FindColumnState({coord.cx + dcx, coord.cz + dcz});
            if (!st || st->state != ColumnLoadState::Ready || !st->column || st->lod > 0)
                return;
            std::vector<int> sections;
            for (int si = 0; si < kSectionCount; ++si)
                if (SectionNeedsDiagonalSeam(*st->column, si, dcx, dcz, m_registry))
                    sections.push_back(si);
            EnqueueSectionRemeshBatch(*st, sections);
        };
        remeshCardinal(-1, 0);
        remeshCardinal(1, 0);
        remeshCardinal(0, -1);
        remeshCardinal(0, 1);
        remeshDiagonal(-1, -1);
        remeshDiagonal(-1, 1);
        remeshDiagonal(1, -1);
        remeshDiagonal(1, 1);
    }

    int VoxelWorld::ProcessReadyMeshUploads(CommandBuffer *cmd, int budget)
    {
        int uploads = 0;
        const ColumnCoord anchorCoord = AnchorToColumn(m_anchor);
        const int anchorSi =
            ClampInt(SectionIndex(static_cast<int>(std::floor(m_anchor.y))), 0, kSectionCount - 1);
        int sectionOrder[kSectionCount];
        for (int i = 0; i < kSectionCount; ++i)
            sectionOrder[i] = i;
        std::sort(sectionOrder, sectionOrder + kSectionCount,
                  [anchorSi](int a, int b)
                  {
                      const int da = std::abs(a - anchorSi);
                      const int db = std::abs(b - anchorSi);
                      if (da != db)
                          return da < db;
                      return a < b;
                  });

        std::vector<uint64_t> keys;
        keys.reserve(m_columns.size());
        for (const auto &entry : m_columns)
            if (entry.second.state == ColumnLoadState::Meshing)
                keys.push_back(entry.first);

        std::sort(keys.begin(), keys.end(), [this, anchorCoord](uint64_t a, uint64_t b)
                  {
                      const ColumnState &ca = m_columns.at(a);
                      const ColumnState &cb = m_columns.at(b);
                      const int da = ColumnDistance(ca.coord, anchorCoord);
                      const int db = ColumnDistance(cb.coord, anchorCoord);
                      if (da != db)
                          return da < db;
                      if (ca.coord.cz != cb.coord.cz)
                          return ca.coord.cz < cb.coord.cz;
                      return ca.coord.cx < cb.coord.cx; });

        for (uint64_t key : keys)
        {
            auto it = m_columns.find(key);
            if (it == m_columns.end())
                continue;

            ColumnState &state = it->second;
            if (state.state != ColumnLoadState::Meshing)
                continue;

            for (int oi = 0; oi < kSectionCount; ++oi)
            {
                const int si = sectionOrder[oi];
                if (state.sectionUploaded[si] || !FutureReady(state.meshFutures[si]))
                    continue;
                if (uploads >= budget)
                    return uploads;

                const MeshData &mesh = state.meshFutures[si].get();
                if (!mesh.vertices.empty() || !mesh.transparentVertices.empty())
                {
                    ArenaHandle opaqueH, transH;
                    if (!UploadSectionMesh(cmd, state.coord, si, mesh, opaqueH, transH))
                        return uploads; // arena OOM — stop this frame so GrowIfNeeded can grow before we retry,
                                        // instead of hammering every remaining ready section with doomed uploads
                    state.handles[si] = opaqueH;
                    state.transparentHandles[si] = transH;
                    ++uploads;
                }
                state.sectionUploaded[si] = true;
                state.sectionLod[si] = static_cast<uint8_t>(state.lod);
                state.meshFutures[si] = std::shared_future<MeshData>();
            }

            bool allUploaded = true;
            for (int si = 0; si < kSectionCount; ++si)
                allUploaded = allUploaded && state.sectionUploaded[si];
            if (allUploaded)
            {
                state.state = ColumnLoadState::Ready;
                // The anchor may have crossed a lod band while this column was meshing — the
                // transition sweep in RequestColumnsForAnchor only sees Ready columns, so re-check here.
                const int lod = DesiredLod(state.coord);
                if (lod != state.lod)
                    RemeshColumnAtLod(state, lod);
                RemeshNeighborSeams(state.coord);
            }
        }

        return uploads;
    }

    bool VoxelWorld::UploadSectionMesh(CommandBuffer *cmd, ColumnCoord coord, int si, const MeshData &mesh,
                                       ArenaHandle &opaqueOut, ArenaHandle &transparentOut)
    {
        opaqueOut = ArenaHandle{};
        transparentOut = ArenaHandle{};

        MeshRuntime runtime{};
        runtime.materialGpuIndex = m_materialGpuIndex;

        const int sectionWorldY = si * kSectionDim;
        const vec3 sectionOrigin(static_cast<float>(coord.cx * kSectionDim),
                                 static_cast<float>(sectionWorldY),
                                 static_cast<float>(coord.cz * kSectionDim));

        if (!mesh.vertices.empty())
        {
            opaqueOut = m_arena.Upload(cmd, mesh, sectionOrigin, m_hostDataOffset, runtime);
            if (!opaqueOut.valid)
                return false; // opaque OOM — caller stops the frame so GrowIfNeeded can grow before retry
        }

        if (!mesh.transparentVertices.empty())
        {
            // Independent second arena mesh tagged transparent (own slot + identity-host transform).
            MeshData water;
            water.vertices = mesh.transparentVertices;
            water.indices = mesh.transparentIndices;
            for (int k = 0; k < 3; ++k)
            {
                water.localMin[k] = mesh.transparentLocalMin[k];
                water.localMax[k] = mesh.transparentLocalMax[k];
            }
            transparentOut = m_arena.Upload(cmd, water, sectionOrigin, m_hostDataOffset, runtime, true);
            // transparent OOM is non-fatal: keep the opaque upload, water for this section retries on remesh.
        }
        return true;
    }

    void VoxelWorld::ReleaseColumn(ColumnState &state)
    {
        PersistColumnIfTouched(state);
        state.state = ColumnLoadState::Unloading;
        for (ArenaHandle &handle : state.handles)
        {
            if (handle.valid)
            {
                m_arena.Release(handle);
                handle = ArenaHandle{};
            }
        }
        for (ArenaHandle &handle : state.transparentHandles)
        {
            if (handle.valid)
            {
                m_arena.Release(handle);
                handle = ArenaHandle{};
            }
        }
    }

    void VoxelWorld::EnqueueSectionRemeshBatch(ColumnState &state, const std::vector<int> &sections)
    {
        if (!state.column || sections.empty())
            return;

        // Sections already remeshing coalesce (dirtyAfterRemesh); the rest share ONE snapshot — taking it
        // per section deep-copied the column + 8 neighbors (~18 MB) each, the dominant stream/edit spike.
        std::vector<int> toMesh;
        toMesh.reserve(sections.size());
        for (int si : sections)
        {
            if (si < 0 || si >= kSectionCount)
                continue;
            if (state.remeshPending[si])
                state.dirtyAfterRemesh[si] = true;
            else
                toMesh.push_back(si);
        }
        if (toMesh.empty())
            return;

        auto columnSnapshot = std::make_shared<ChunkColumn>(*state.column);
        auto neighbors = std::make_shared<ColumnNeighbors>(state.lod == 0 ? GatherNeighborSnapshots(state.coord)
                                                                          : ColumnNeighbors{});
        auto registrySnapshot = std::make_shared<BlockRegistry>(m_registry);
        const ColumnCoord coord = state.coord;
        const int lod = state.lod;
        for (int si : toMesh)
        {
            state.remeshFutures[si] = ThreadPool::General.Enqueue(
                [columnSnapshot, neighbors, registrySnapshot, coord, si, lod]() -> MeshData
                { return MeshSectionCpu(*columnSnapshot, *registrySnapshot, si, coord, *neighbors, lod); });
            state.remeshPending[si] = true;
            state.remeshLod[si] = static_cast<uint8_t>(lod);
        }
    }

    void VoxelWorld::ProcessDirtyRemeshResults(CommandBuffer *cmd, int applyBudget)
    {
        int applied = 0; // section uploads applied this frame; caps the per-frame remesh burst
        for (auto &entry : m_columns)
        {
            ColumnState &state = entry.second;
            if (state.state != ColumnLoadState::Ready)
                continue;

            for (int si = 0; si < kSectionCount; ++si)
            {
                if (applied >= applyBudget)
                    return; // budget spent — ready futures stay pending and apply next frame
                if (!state.remeshPending[si] || !FutureReady(state.remeshFutures[si]))
                    continue;

                const MeshData &mesh = state.remeshFutures[si].get();
                const bool sectionHasBlocks = state.column && !state.column->Section(si).IsEmpty();
                const bool meshEmpty = mesh.vertices.empty() && mesh.transparentVertices.empty();
                const bool hasAnyHandle = state.handles[si].valid || state.transparentHandles[si].valid;
                // Keep-old only applies same-lod: across a lod change an empty result is legitimate
                // (e.g. coarse wall caps mesh to nothing at lod 0) and the old mesh must release below.
                if (meshEmpty && sectionHasBlocks && hasAnyHandle && state.remeshLod[si] == state.sectionLod[si])
                {
                    state.remeshFutures[si] = std::shared_future<MeshData>();
                    state.remeshPending[si] = false;
                    if (state.dirtyAfterRemesh[si])
                    {
                        state.dirtyAfterRemesh[si] = false;
                        MarkSectionDirty(state.coord, si);
                    }
                    continue;
                }

                if (!meshEmpty)
                {
                    // Upload the replacement BEFORE releasing the old mesh. On opaque arena OOM keep the old
                    // handles (stay visible) and retry next frame after GrowIfNeeded — releasing first would
                    // drop the section to invisible until its next edit (remeshPending is cleared below).
                    ArenaHandle opaqueH, transH;
                    if (!UploadSectionMesh(cmd, state.coord, si, mesh, opaqueH, transH))
                        continue;
                    if (state.handles[si].valid)
                        m_arena.Release(state.handles[si]);
                    if (state.transparentHandles[si].valid)
                        m_arena.Release(state.transparentHandles[si]);
                    state.handles[si] = opaqueH;
                    state.transparentHandles[si] = transH;
                    ++applied; // count only the expensive staging upload toward the per-frame budget
                }
                else
                {
                    if (state.handles[si].valid)
                    {
                        m_arena.Release(state.handles[si]);
                        state.handles[si] = ArenaHandle{};
                    }
                    if (state.transparentHandles[si].valid)
                    {
                        m_arena.Release(state.transparentHandles[si]);
                        state.transparentHandles[si] = ArenaHandle{};
                    }
                }

                state.sectionLod[si] = state.remeshLod[si];
                state.remeshFutures[si] = std::shared_future<MeshData>();
                state.remeshPending[si] = false;

                if (state.dirtyAfterRemesh[si])
                {
                    state.dirtyAfterRemesh[si] = false;
                    MarkSectionDirty(state.coord, si);
                }
            }
        }
    }

    void VoxelWorld::MarkSectionDirty(ColumnCoord coord, int si)
    {
        if (si < 0 || si >= kSectionCount)
            return;
        const uint64_t key = ColumnKey(coord);
        for (const auto &d : m_dirtySections)
            if (d.first == key && d.second == si)
                return; // already pending
        m_dirtySections.emplace_back(key, si);
    }

    void VoxelWorld::MarkEditDirtySections(ColumnCoord coord, int wx, int y, int wz)
    {
        const int si = SectionIndex(y);
        const int ly = LocalY(y);
        const int lx = LocalX(wx);
        const int lz = LocalZ(wz);

        TouchSection(coord, si);
        MarkSectionDirty(coord, si);

        if (ly == 0 && si > 0)
        {
            TouchSection(coord, si - 1);
            MarkSectionDirty(coord, si - 1);
        }
        else if (ly == kSectionDim - 1 && si + 1 < kSectionCount)
        {
            TouchSection(coord, si + 1);
            MarkSectionDirty(coord, si + 1);
        }

        auto markNeighbor = [&](int dcx, int dcz)
        {
            const ColumnCoord neighbor{coord.cx + dcx, coord.cz + dcz};
            TouchSection(neighbor, si);
            MarkSectionDirty(neighbor, si);
            if (ly == 0 && si > 0)
            {
                TouchSection(neighbor, si - 1);
                MarkSectionDirty(neighbor, si - 1);
            }
            else if (ly == kSectionDim - 1 && si + 1 < kSectionCount)
            {
                TouchSection(neighbor, si + 1);
                MarkSectionDirty(neighbor, si + 1);
            }
        };

        if (lx == 0)
            markNeighbor(-1, 0);
        if (lx == kSectionDim - 1)
            markNeighbor(1, 0);
        if (lz == 0)
            markNeighbor(0, -1);
        if (lz == kSectionDim - 1)
            markNeighbor(0, 1);
        if (lx == 0 && lz == 0)
            markNeighbor(-1, -1);
        if (lx == 0 && lz == kSectionDim - 1)
            markNeighbor(-1, 1);
        if (lx == kSectionDim - 1 && lz == 0)
            markNeighbor(1, -1);
        if (lx == kSectionDim - 1 && lz == kSectionDim - 1)
            markNeighbor(1, 1);
    }

    void VoxelWorld::RemeshDirtySections(CommandBuffer *cmd, int applyBudget)
    {
        std::unordered_map<uint64_t, std::vector<int>> byColumn;
        for (const auto &dirty : m_dirtySections)
            byColumn[dirty.first].push_back(dirty.second);

        std::vector<std::pair<uint64_t, int>> remaining;
        for (auto &group : byColumn)
        {
            auto colIt = m_columns.find(group.first);
            if (colIt == m_columns.end())
                continue;

            ColumnState &state = colIt->second;
            if (state.state != ColumnLoadState::Ready || !state.column)
            {
                for (int si : group.second)
                    remaining.emplace_back(group.first, si);
                continue;
            }

            EnqueueSectionRemeshBatch(state, group.second);
        }

        m_dirtySections.swap(remaining);
        ProcessDirtyRemeshResults(cmd, applyBudget);
    }

    void VoxelWorld::RetireSubmittedUpdateCommands(bool all)
    {
        while (!m_submittedUpdateCmds.empty() &&
               (all || m_submittedUpdateCmds.size() > kMaxPendingUpdateCommands))
        {
            CommandBuffer *cmd = m_submittedUpdateCmds.front();
            cmd->Wait();
            cmd->Return();
            m_submittedUpdateCmds.erase(m_submittedUpdateCmds.begin());
        }
    }

    void VoxelWorld::TouchSection(ColumnCoord coord, int si)
    {
        if (si < 0 || si >= kSectionCount)
            return;
        ColumnState *state = FindColumnState(coord);
        if (!state)
            return;
        state->touchedSectionMask |= static_cast<uint16_t>(1u << si);
    }

    void VoxelWorld::PersistColumnIfTouched(ColumnState &state)
    {
        if (m_saveRoot.empty() || state.touchedSectionMask == 0 || !state.column)
            return;
        if (ColumnChunkStore::Save(m_saveRoot, *state.column, state.touchedSectionMask))
        {
            PE_INFO("VoxelWorld: saved column (%d, %d)", state.coord.cx, state.coord.cz);
        }
    }

    void VoxelWorld::PersistAllTouchedColumns()
    {
        if (m_saveRoot.empty())
            return;
        for (auto &entry : m_columns)
            PersistColumnIfTouched(entry.second);
    }

    bool VoxelWorld::SaveAllModified()
    {
        if (m_saveRoot.empty())
            return false;
        PersistAllTouchedColumns();
        return true;
    }
} // namespace pe::voxel
