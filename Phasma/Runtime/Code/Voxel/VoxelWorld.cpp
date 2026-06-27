#include "Voxel/VoxelWorld.h"
#include "Voxel/GreedyMesher.h"
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

        MeshData MeshSectionCpu(const ChunkColumn &column, const BlockRegistry &registry, int si)
        {
            const int sectionWorldY = si * kSectionDim;
            GreedyMesher mesher;
            BlockSampler sampler = [&column, sectionWorldY](int lx, int ly, int lz) -> BlockId
            {
                if (lx < 0 || lx >= kSectionDim || ly < 0 || ly >= kSectionDim || lz < 0 || lz >= kSectionDim)
                    return kAir;
                return column.GetLocal(lx, sectionWorldY + ly, lz);
            };

            return mesher.Mesh(sampler, registry, 0);
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
        m_anchor = vec3(0.0f);

        // Engine ships a default noise generator; a game keeps its own by calling
        // SetTerrainGenerator before Create (m_generatorOverridden guards it from this default).
        if (!m_generatorOverridden)
            m_generator = std::make_shared<NoiseGen>(m_cfg.groundY);

        RegisterDefaultBlocks();
        CreateHostMesh();

        m_scene->FlushPendingGpuWork();

        // Build the voxel material (atlas upload + Scene::UpdateTextures) BEFORE reserving arena
        // capacity. UpdateTextures rebuilds m_meshConstants/materialTable to the NON-arena size; if it
        // ran after arena.Init it would shrink the arena's reservation and overflow the per-section
        // mesh-constants write (Buffer::CopyDataRaw range overflow). Arena reservation must be the LAST
        // buffer-sizing op before streaming starts.
        m_voxelMaterial = std::make_unique<VoxelMaterial>();
        m_voxelMaterial->Build(m_scene, {Path::RuntimeAssets + "Textures/Voxel/grass.png",
                                         Path::RuntimeAssets + "Textures/Voxel/dirt.png",
                                         Path::RuntimeAssets + "Textures/Voxel/stone.png"});
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
        // Per-section budget for the shared, pre-reserved arena pool (total = gridSections * budget;
        // cannot grow live without destroying the arena). A flat greedy section is ~24 verts, but
        // AO-aware merging (fewer merges near edges) + carved cave interiors (lots of new wall faces)
        // push feature/cave sections to ~1-4k verts, so the pool is sized for that, not the flat case.
        // Raising this pre-grows the Scene geometry buffer (Vertex+PositionUvVertex ~= 148 B/vert), so
        // a loadRadius-6 grid reserves ~700 MB at 1024. ponytail: a uniform per-slot reservation
        // over-provisions the always-empty sky/solid sections; size by expected non-empty fraction (or
        // grow on unload-pressure) if VRAM matters. A section over budget fails to upload (logged) and
        // leaves a hole, so lower cave density / loadRadius before dropping this.
        const uint32_t kVertsPerSection = 1024u;
        const uint32_t kIndicesPerSection = 1536u;
        const uint32_t vtxCapVertices = gridSections * kVertsPerSection;
        const uint32_t idxCapBytes = gridSections * kIndicesPerSection * static_cast<uint32_t>(sizeof(uint32_t));

        m_arena.Init(m_scene, vtxCapVertices, idxCapBytes, gridSections);
        SetAnchor(m_anchor);
    }

    void VoxelWorld::Destroy()
    {
        RetireSubmittedUpdateCommands(true);

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
        m_scene = nullptr;
    }

    void VoxelWorld::SetAnchor(const vec3 &worldPos)
    {
        m_anchor = worldPos;
        if (m_scene && m_arena.IsInitialized())
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
        MarkSectionDirty(coord, SectionIndex(y));
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

        Queue *queue = RHII.GetMainQueue();
        if (!queue)
            return;

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        m_arena.Update(cmd);
        ProcessReadyMeshUploads(cmd, m_cfg.uploadBudgetPerFrame);
        RemeshDirtySections(cmd);
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
        state.generationFuture = ThreadPool::General.Enqueue(
            [coord, gen]() -> ChunkColumn
            {
                ChunkColumn column(coord);
                if (gen)
                    gen->Generate(column);
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
            ApplyPendingEdits(key, *state.column);
            state.state = ColumnLoadState::Generated;
            EnqueueColumnMeshing(state);
        }
    }

    void VoxelWorld::ApplyPendingEdits(uint64_t key, ChunkColumn &column)
    {
        auto editsIt = m_pendingEdits.find(key);
        if (editsIt == m_pendingEdits.end())
            return;

        for (const PendingEdit &edit : editsIt->second)
        {
            if (edit.y >= 0 && edit.y < kWorldHeight)
                column.SetLocal(LocalX(edit.x), edit.y, LocalZ(edit.z), edit.id);
        }
        m_pendingEdits.erase(editsIt);
    }

    void VoxelWorld::EnqueueColumnMeshing(ColumnState &state)
    {
        if (!state.column)
            return;

        auto columnSnapshot = std::make_shared<ChunkColumn>(*state.column);
        auto registrySnapshot = std::make_shared<BlockRegistry>(m_registry);
        for (int si = 0; si < kSectionCount; ++si)
        {
            state.sectionUploaded[si] = false;
            state.meshFutures[si] = ThreadPool::General.Enqueue([columnSnapshot, registrySnapshot, si]() -> MeshData
                                                                { return MeshSectionCpu(*columnSnapshot, *registrySnapshot, si); });
        }
        state.state = ColumnLoadState::Meshing;
    }

    int VoxelWorld::ProcessReadyMeshUploads(CommandBuffer *cmd, int budget)
    {
        int uploads = 0;
        const ColumnCoord anchorCoord = AnchorToColumn(m_anchor);
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

            for (int si = 0; si < kSectionCount; ++si)
            {
                if (state.sectionUploaded[si] || !FutureReady(state.meshFutures[si]))
                    continue;
                if (uploads >= budget)
                    return uploads;

                const MeshData &mesh = state.meshFutures[si].get();
                if (!mesh.vertices.empty())
                {
                    state.handles[si] = UploadSectionMesh(cmd, state.coord, si, mesh);
                    ++uploads;
                }
                state.sectionUploaded[si] = true;
                state.meshFutures[si] = std::shared_future<MeshData>();
            }

            bool allUploaded = true;
            for (int si = 0; si < kSectionCount; ++si)
                allUploaded = allUploaded && state.sectionUploaded[si];
            if (allUploaded)
                state.state = ColumnLoadState::Ready;
        }

        return uploads;
    }

    ArenaHandle VoxelWorld::UploadSectionMesh(CommandBuffer *cmd, ColumnCoord coord, int si, const MeshData &mesh)
    {
        if (mesh.vertices.empty())
            return ArenaHandle{};

        MeshRuntime runtime{};
        runtime.materialGpuIndex = m_materialGpuIndex;

        const int sectionWorldY = si * kSectionDim;
        const vec3 sectionOrigin(static_cast<float>(coord.cx * kSectionDim),
                                 static_cast<float>(sectionWorldY),
                                 static_cast<float>(coord.cz * kSectionDim));
        return m_arena.Upload(cmd, mesh, sectionOrigin, m_hostDataOffset, runtime);
    }

    void VoxelWorld::ReleaseColumn(ColumnState &state)
    {
        state.state = ColumnLoadState::Unloading;
        for (ArenaHandle &handle : state.handles)
        {
            if (handle.valid)
            {
                m_arena.Release(handle);
                handle = ArenaHandle{};
            }
        }
    }

    void VoxelWorld::StartDirtySectionRemesh(ColumnState &state, int si)
    {
        if (!state.column || si < 0 || si >= kSectionCount)
            return;
        if (state.remeshPending[si])
        {
            state.dirtyAfterRemesh[si] = true;
            return;
        }

        auto columnSnapshot = std::make_shared<ChunkColumn>(*state.column);
        auto registrySnapshot = std::make_shared<BlockRegistry>(m_registry);
        state.remeshFutures[si] = ThreadPool::General.Enqueue([columnSnapshot, registrySnapshot, si]() -> MeshData
                                                              { return MeshSectionCpu(*columnSnapshot, *registrySnapshot, si); });
        state.remeshPending[si] = true;
    }

    void VoxelWorld::ProcessDirtyRemeshResults(CommandBuffer *cmd)
    {
        for (auto &entry : m_columns)
        {
            ColumnState &state = entry.second;
            if (state.state != ColumnLoadState::Ready)
                continue;

            for (int si = 0; si < kSectionCount; ++si)
            {
                if (!state.remeshPending[si] || !FutureReady(state.remeshFutures[si]))
                    continue;

                const MeshData &mesh = state.remeshFutures[si].get();
                if (state.handles[si].valid)
                {
                    m_arena.Release(state.handles[si]);
                    state.handles[si] = ArenaHandle{};
                }
                if (!mesh.vertices.empty())
                    state.handles[si] = UploadSectionMesh(cmd, state.coord, si, mesh);

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

    void VoxelWorld::RemeshDirtySections(CommandBuffer *cmd)
    {
        std::vector<std::pair<uint64_t, int>> remaining;
        remaining.reserve(m_dirtySections.size());

        for (const auto &dirty : m_dirtySections)
        {
            auto colIt = m_columns.find(dirty.first);
            if (colIt == m_columns.end())
                continue;

            ColumnState &state = colIt->second;
            const int si = dirty.second;
            if (state.state != ColumnLoadState::Ready || !state.column)
            {
                remaining.push_back(dirty);
                continue;
            }

            if (state.remeshPending[si])
            {
                state.dirtyAfterRemesh[si] = true;
                continue;
            }

            StartDirtySectionRemesh(state, si);
        }

        m_dirtySections.swap(remaining);
        ProcessDirtyRemeshResults(cmd);
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
} // namespace pe::voxel
