#include "Voxel/VoxelWorld.h"
#include "Voxel/FlatGen.h"
#include "Voxel/GreedyMesher.h"
#include "Voxel/VoxelCollider.h"
#include "Voxel/VoxelMaterial.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "Base/Path.h"
#include "Scene/Material.h"
#include "Scene/Scene.h"
#include "Scene/SceneNode.h"

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

    ChunkColumn *VoxelWorld::FindColumn(ColumnCoord coord)
    {
        auto it = m_columns.find(ColumnKey(coord));
        return it == m_columns.end() ? nullptr : &it->second;
    }

    const ChunkColumn *VoxelWorld::FindColumn(ColumnCoord coord) const
    {
        auto it = m_columns.find(ColumnKey(coord));
        return it == m_columns.end() ? nullptr : &it->second;
    }

    void VoxelWorld::Create(Scene *scene, const VoxelConfig &cfg)
    {
        Destroy();

        PE_ERROR_IF(!scene, "VoxelWorld::Create requires a valid Scene");
        if (!scene)
            return;

        m_scene = scene;
        m_cfg = cfg;
        if (m_cfg.loadRadius < 0)
            m_cfg.loadRadius = 0;
        m_anchor = vec3(0.0f);

        RegisterDefaultBlocks();
        CreateHostMesh();

        m_scene->FlushPendingGpuWork();

        // Build the voxel material (atlas upload + Scene::UpdateTextures) BEFORE reserving arena
        // capacity. UpdateTextures rebuilds m_meshConstants/materialTable to the NON-arena size; if it
        // ran after arena.Init it would shrink the arena's reservation and overflow the per-section
        // mesh-constants write (Buffer::CopyDataRaw range overflow). Arena reservation must be the LAST
        // buffer-sizing op before UploadInitialGrid.
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

        const int gridDim = m_cfg.loadRadius * 2 + 1;
        const uint32_t gridSections = static_cast<uint32_t>(gridDim * gridDim * kSectionCount);
        // Greedy-meshed sections are small: a flat-ground section is ~24 verts / 36 indices, and even
        // busy terrain stays in the low hundreds. Reserve a generous-but-sane per-section budget — NOT
        // the theoretical per-block max (4096) — so the shared geometry buffer is not pre-grown by
        // hundreds of MB for a sparsely-populated grid (Vertex+PositionUvVertex ~= 148 B/vert). A
        // section that exceeds this budget simply fails to upload (logged); raise it for dense terrain.
        const uint32_t kVertsPerSection = 256u;
        const uint32_t kIndicesPerSection = 384u;
        const uint32_t vtxCapVertices = gridSections * kVertsPerSection;
        const uint32_t idxCapBytes = gridSections * kIndicesPerSection * static_cast<uint32_t>(sizeof(uint32_t));

        m_arena.Init(m_scene, vtxCapVertices, idxCapBytes, gridSections);

        UploadInitialGrid();
    }

    void VoxelWorld::Destroy()
    {
        RetireSubmittedUpdateCommands(true);

        if (m_scene)
            m_scene->SetVoxelAtlasView(nullptr);
        m_voxelMaterial.reset();

        if (m_scene && m_arena.IsInitialized() && !m_sections.empty())
        {
            Queue *queue = RHII.GetMainQueue();
            if (queue)
            {
                CommandBuffer *cmd = queue->AcquireCommandBuffer();
                cmd->Begin();
                for (const SectionHandle &section : m_sections)
                    m_arena.Release(section.handle);
                m_arena.Update(cmd);
                cmd->End();
                queue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                cmd->Return();
            }
        }

        m_arena.Destroy();
        m_sections.clear();
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
        const ColumnCoord coord = WorldToColumn(x, z);
        ChunkColumn *column = FindColumn(coord);
        if (!column)
            return;

        column->SetLocal(LocalX(x), y, LocalZ(z), id);
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

        Queue *queue = RHII.GetMainQueue();
        if (!queue)
            return;

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        m_arena.Update(cmd);
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
        m_registry.Register({kStoneBlock, "stone", true, true, VoxelRenderClass::Opaque, {0, 0, 0, 0, 0, 0}});
        m_registry.Register({kDirtBlock, "dirt", true, true, VoxelRenderClass::Opaque, {0, 0, 0, 0, 0, 0}});
        m_registry.Register({kGrassBlock, "grass", true, true, VoxelRenderClass::Opaque, {0, 0, 0, 0, 0, 0}});
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

    void VoxelWorld::UploadInitialGrid()
    {
        Queue *queue = RHII.GetMainQueue();
        PE_ERROR_IF(!queue, "VoxelWorld::UploadInitialGrid requires the main queue");
        if (!queue)
            return;

        GreedyMesher mesher;
        FlatGen generator(m_cfg.groundY, kStoneBlock);

        CommandBuffer *cmd = queue->AcquireCommandBuffer();
        cmd->Begin();
        m_arena.Update(cmd);

        const int radius = m_cfg.loadRadius;
        for (int cz = -radius; cz <= radius; ++cz)
        {
            for (int cx = -radius; cx <= radius; ++cx)
            {
                const ColumnCoord coord{cx, cz};
                auto inserted = m_columns.emplace(ColumnKey(coord), ChunkColumn(coord));
                ChunkColumn &column = inserted.first->second;
                generator.Generate(column);

                for (int si = 0; si < kSectionCount; ++si)
                {
                    const int sectionWorldY = si * kSectionDim;
                    // Milestone B samples all cross-section and cross-column neighbors as air, which
                    // over-generates boundary faces but keeps the first arena-streaming slice simple.
                    BlockSampler sampler = [&column, sectionWorldY](int lx, int ly, int lz) -> BlockId
                    {
                        if (lx < 0 || lx >= kSectionDim ||
                            ly < 0 || ly >= kSectionDim ||
                            lz < 0 || lz >= kSectionDim)
                        {
                            return kAir;
                        }
                        return column.GetLocal(lx, sectionWorldY + ly, lz);
                    };

                    MeshData mesh = mesher.Mesh(sampler, m_registry, 0);
                    if (mesh.vertices.empty())
                        continue;

                    MeshRuntime runtime{};
                    runtime.materialGpuIndex = m_materialGpuIndex;

                    const vec3 sectionOrigin(static_cast<float>(cx * kSectionDim),
                                             static_cast<float>(sectionWorldY),
                                             static_cast<float>(cz * kSectionDim));
                    ArenaHandle handle = m_arena.Upload(cmd, mesh, sectionOrigin, m_hostDataOffset, runtime);
                    if (handle.valid)
                        m_sections.push_back({coord, si, handle});
                }
            }
        }

        cmd->End();
        queue->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
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
