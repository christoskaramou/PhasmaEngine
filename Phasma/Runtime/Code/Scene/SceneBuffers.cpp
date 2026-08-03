#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/MaterialReflection.h"
#include "Scene/MeshConstants.h"
#include "Scene/ModelAsset.h"
#include "Scene/PassInfoAsset.h"
#include "Scene/SceneRuntimeHooks.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Vertex.h"

namespace pe
{
    static bool IsRasterIndirectMesh(const Mesh &mesh)
    {
        return mesh.renderType != RenderType::Lines && mesh.renderType != RenderType::SpriteOutline;
    }

    bool Scene::HasSelectedRenderableMeshes() const
    {
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            NodeId *node = m_nodeIds[i];
            if (!node || m_nodeRuntime[i].gpuPending || !IsNodeHierarchyEnabled(node) || !IsSceneNodeSelected(node))
                continue;

            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (!IsValidMeshIndex(meshIdx))
                    continue;

                const Mesh &mesh = m_meshes[meshIdx];
                if (mesh.indexCount != 0 && mesh.renderType != RenderType::Lines &&
                    mesh.renderType != RenderType::SpriteOutline)
                    return true;
            }
        }

        return false;
    }

    void Scene::UpdateMeshSelectionFlags()
    {
        if (!m_meshConstants || m_meshCount == 0)
            return;

        // In-place rewrite of the editorFlags field for every mesh, in the SAME iteration order
        // CreateMeshConstants uses (so per-mesh offsets line up). Lets selection changes reach the
        // GPU selected bucket each frame without a full geometry rebuild. Vulkan reads m_meshConstants
        // directly; DX12 reads the m_meshConstantsDevice mirror, republished below when selection changes.
        size_t offset = 0;
        uint32_t meshOrdinal = 0;
        uint64_t selectionSignature = 0; // XOR of a per-ordinal hash for each SELECTED mesh
        m_meshConstants->Map();

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending || !IsNodeHierarchyEnabled(m_nodeIds[i]))
                continue;

            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (!IsValidMeshIndex(meshIdx))
                    continue;

                const Mesh &mesh = m_meshes[meshIdx];
                if (!IsRasterIndirectMesh(mesh) || mesh.indexCount == 0)
                    continue;

                uint32_t flags = 0;
                if (IsSceneNodeSelected(m_nodeIds[i]))
                {
                    flags |= 1u;
                    // splitmix64 of the ordinal. XOR-accumulating distinct terms flips the signature
                    // whenever any single mesh's selected state toggles (no collision on add/remove).
                    uint64_t h = meshOrdinal + 1u;
                    h ^= h >> 30;
                    h *= 0xbf58476d1ce4e5b9ull;
                    h ^= h >> 27;
                    h *= 0x94d049bb133111ebull;
                    h ^= h >> 31;
                    selectionSignature ^= h;
                }
                if (mesh.material && mesh.material->doubleSided)
                    flags |= 2u;
                if (mesh.material && mesh.material->terrain)
                    flags |= 16u; // dedicated terrain triplanar pipeline (cull bucket 8)

                BufferRange range{};
                range.data = &flags;
                range.offset = offset + offsetof(Mesh_Constants, editorFlags);
                range.size = sizeof(flags);
                m_meshConstants->Copy(1, &range, true);

                offset += sizeof(Mesh_Constants);
                ++meshOrdinal;
            }
        }

        m_meshConstants->Flush(offset, 0);
        m_meshConstants->Unmap();

        // DX12: the cull pass reads m_meshConstantsDevice (a GPU-cached mirror), so the host writes
        // above are invisible to it — without this the selected bucket keeps whatever was baked at the
        // last geometry rebuild, so the outline stays on the previously selected mesh. Republish the
        // mirror ONLY when the selected set changed: this runs every frame and a per-frame device
        // copy+wait would stall the cull hot path.
        if (m_meshConstantsDevice && offset > 0 && selectionSignature != m_meshSelectionMirrorSignature)
        {
            m_meshSelectionMirrorSignature = selectionSignature;
            Queue *queue = RHII.GetMainQueue();
            CommandBuffer *cmd = queue->AcquireCommandBuffer();
            cmd->Begin();
            cmd->CopyBuffer(m_meshConstants, m_meshConstantsDevice, offset, 0, 0);
            BufferBarrierInfo barrier{};
            barrier.buffer = m_meshConstantsDevice;
            barrier.stageMask = PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_SHADER;
            barrier.accessMask = PE_ACCESS_SHADER_STORAGE_READ;
            barrier.offset = 0;
            barrier.size = offset;
            cmd->BufferBarrier(barrier);
            m_meshConstantsDevice->GetTrackInfo() = barrier;
            cmd->End();
            queue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            queue->ReturnCommandBuffer(cmd);
        }
    }

    void Scene::UploadBuffers(CommandBuffer *cmd)
    {
        DestroyBuffers();
        CreateGeometryBuffer();
        CopyIndices(cmd);
        CopyVertices(cmd);
        CreateStorageBuffers();
        MarkUniformsDirty();
        CreateIndirectBuffers(cmd);
        UpdateImageViews();
        CreateMaterialTable();
        CreateMeshConstants(cmd);

        MemoryBarrierInfo geometryUploadBarrier{};
        geometryUploadBarrier.srcStageMask = PE_STAGE_TRANSFER;
        geometryUploadBarrier.srcAccessMask = PE_ACCESS_TRANSFER_WRITE;
        geometryUploadBarrier.dstStageMask = PE_STAGE_VERTEX_INPUT;
        geometryUploadBarrier.dstAccessMask = PE_ACCESS_INDEX_READ | PE_ACCESS_VERTEX_ATTRIBUTE_READ;
        cmd->MemoryBarrier(geometryUploadBarrier);

        // Geometry buffer was recreated — existing BLAS handles are invalid.
        // Mark dirty so FlushPendingGpuWork rebuilds acceleration structures.
        if (RHII.GetCaps().rayTracing)
            m_blasDirty = true;
    }

    void Scene::CreateGeometryBuffer()
    {
        m_meshCount = 0;
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending || !IsNodeHierarchyEnabled(m_nodeIds[i]))
                continue;
            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (IsValidMeshIndex(meshIdx) &&
                    m_meshes[meshIdx].indexCount > 0 &&
                    IsRasterIndirectMesh(m_meshes[meshIdx]))
                {
                    m_meshCount++;
                }
            }
        }

        m_indicesCount = static_cast<uint32_t>(m_indexStore.size());
        m_verticesCount = static_cast<uint32_t>(m_vertexStore.size());
        m_positionsCount = static_cast<uint32_t>(m_positionUvStore.size());
        m_aabbVerticesCount = static_cast<uint32_t>(m_aabbVertexStore.size());

        m_aabbIndicesOffset = m_indicesCount * sizeof(uint32_t);
        m_verticesOffset = m_aabbIndicesOffset + s_aabbIndices.size() * sizeof(uint32_t);
        m_positionsOffset = m_verticesOffset + m_verticesCount * sizeof(Vertex);
        m_aabbVerticesOffset = m_positionsOffset + m_positionsCount * sizeof(PositionUvVertex);

        // TRANSFER_SRC: GeometryArena::ReserveArenaCapacity copies the existing geometry out of this
        // buffer into a larger one when it reserves arena headroom (SceneBuffers.cpp ReserveArenaCapacity).
        PeBufferUsageFlags geometryUsage = PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_TRANSFER_SRC |
                                           PE_BUFFER_USAGE_INDEX_BUFFER | PE_BUFFER_USAGE_VERTEX_BUFFER |
                                           PE_BUFFER_USAGE_STORAGE_BUFFER;
        if (RHII.GetCaps().rayTracing)
            geometryUsage |= PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS |
                             PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR;

        m_buffer = Buffer::Create({
            .size = m_aabbVerticesOffset + sizeof(AabbVertex) * m_aabbVerticesCount,
            .usage = geometryUsage,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = "combined_Geometry_buffer",
        });
    }

    void Scene::BindDrawIdBuffer(CommandBuffer *cmd) const
    {
        if (RHII.GetApi() != PE_GRAPHICS_API_DX12)
            return;

        PE_ERROR_IF(!m_indirectAll, "Scene draw-ID buffer is not initialized");
        cmd->BindVertexBuffer(m_indirectAll, 0, 1, 1);
    }

    void Scene::CopyIndices(CommandBuffer *cmd)
    {
        if (m_indicesCount > 0)
        {
            cmd->CopyBufferStaged(m_buffer, m_indexStore.data(), m_indicesCount * sizeof(uint32_t), 0);

            BufferBarrierInfo indexBarrierInfo{};
            indexBarrierInfo.buffer = m_buffer;
            indexBarrierInfo.stageMask = PE_STAGE_VERTEX_INPUT;
            indexBarrierInfo.accessMask = PE_ACCESS_INDEX_READ;
            indexBarrierInfo.size = m_indicesCount * sizeof(uint32_t);
            indexBarrierInfo.offset = 0;
            cmd->BufferBarrier(indexBarrierInfo);
        }

        cmd->CopyBufferStaged(m_buffer, s_aabbIndices.data(), s_aabbIndices.size() * sizeof(uint32_t), m_aabbIndicesOffset);

        if (s_aabbIndices.size() > 0)
        {
            BufferBarrierInfo aabbIndexBarrierInfo{};
            aabbIndexBarrierInfo.buffer = m_buffer;
            aabbIndexBarrierInfo.stageMask = PE_STAGE_VERTEX_INPUT;
            aabbIndexBarrierInfo.accessMask = PE_ACCESS_INDEX_READ;
            aabbIndexBarrierInfo.size = s_aabbIndices.size() * sizeof(uint32_t);
            aabbIndexBarrierInfo.offset = m_aabbIndicesOffset;
            cmd->BufferBarrier(aabbIndexBarrierInfo);
        }
    }

    void Scene::CopyVertices(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<SceneSettings>();
        auto &progress = gSettings.loading.current;
        auto &total = gSettings.loading.total;

        total = m_verticesCount + m_positionsCount + m_aabbVerticesCount;
        progress = 0;
        gSettings.loading.SetName("Uploading to GPU");

        if (m_verticesCount > 0)
        {
            cmd->CopyBufferStaged(m_buffer, m_vertexStore.data(), m_verticesCount * sizeof(Vertex), m_verticesOffset);
            progress += m_verticesCount;

            BufferBarrierInfo vertexBarrierInfo{};
            vertexBarrierInfo.buffer = m_buffer;
            vertexBarrierInfo.stageMask = PE_STAGE_VERTEX_INPUT;
            vertexBarrierInfo.accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ;
            vertexBarrierInfo.size = m_verticesCount * sizeof(Vertex);
            vertexBarrierInfo.offset = m_verticesOffset;
            cmd->BufferBarrier(vertexBarrierInfo);
        }

        if (m_positionsCount > 0)
        {
            cmd->CopyBufferStaged(m_buffer, m_positionUvStore.data(), m_positionsCount * sizeof(PositionUvVertex), m_positionsOffset);
            progress += m_positionsCount;

            BufferBarrierInfo posVertexBarrierInfo{};
            posVertexBarrierInfo.buffer = m_buffer;
            posVertexBarrierInfo.stageMask = PE_STAGE_VERTEX_INPUT;
            posVertexBarrierInfo.accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ;
            posVertexBarrierInfo.size = m_positionsCount * sizeof(PositionUvVertex);
            posVertexBarrierInfo.offset = m_positionsOffset;
            cmd->BufferBarrier(posVertexBarrierInfo);
        }

        if (m_aabbVerticesCount > 0)
        {
            cmd->CopyBufferStaged(m_buffer, m_aabbVertexStore.data(), m_aabbVerticesCount * sizeof(AabbVertex), m_aabbVerticesOffset);
            progress += m_aabbVerticesCount;

            BufferBarrierInfo aabbVertexBarrierInfo{};
            aabbVertexBarrierInfo.buffer = m_buffer;
            aabbVertexBarrierInfo.stageMask = PE_STAGE_VERTEX_INPUT;
            aabbVertexBarrierInfo.accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ;
            aabbVertexBarrierInfo.size = m_aabbVerticesCount * sizeof(AabbVertex);
            aabbVertexBarrierInfo.offset = m_aabbVerticesOffset;
            cmd->BufferBarrier(aabbVertexBarrierInfo);
        }
    }

    void Scene::CreateStorageBuffers()
    {
        size_t storageSize = sizeof(PerFrameData);
        const int maxJointCount = GetMaxJointCount();

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            m_nodeRuntime[i].hasUniformData = false;
            m_nodeRuntime[i].dataOffset = static_cast<size_t>(-1);

            if (m_nodeRuntime[i].gpuPending || !IsNodeHierarchyEnabled(m_nodeIds[i]))
                continue;

            bool hasDrawable = false;
            for (int mr : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (IsValidMeshIndex(mr) && m_meshes[mr].indexCount > 0)
                {
                    hasDrawable = true;
                    break;
                }
            }
            if (!hasDrawable)
                continue;

            m_nodeRuntime[i].hasUniformData = true;
            m_nodeRuntime[i].dataOffset = storageSize;
            const bool skinned = NodeHasSkinnedMesh(m_nodeIds[i]);
            int jointCount = skinned ? GetJointCountForNode(m_nodeIds[i]) : 0;
            if (jointCount <= 0 && skinned)
                jointCount = maxJointCount;
            size_t nodeDataSize = sizeof(NodeGpuData) + jointCount * sizeof(mat4);
            storageSize += nodeDataSize;
        }

        if (m_storagesDevice.size() != m_storages.size())
            m_storagesDevice.resize(m_storages.size(), nullptr);

        const PeBufferUsageFlags storageUsage =
            PE_BUFFER_USAGE_STORAGE_BUFFER |
            (RHII.GetApi() == PE_GRAPHICS_API_DX12 ? PE_BUFFER_USAGE_TRANSFER_SRC : PE_BUFFER_USAGE_NONE);
        const bool useStorageDeviceMirror = RHII.GetApi() == PE_GRAPHICS_API_DX12;

        for (uint32_t i = 0; i < m_storages.size(); i++)
        {
            auto &storage = m_storages[i];
            if (storage)
            {
                RHII.AddToDeletionQueue([b = storage]()
                                        { Buffer* buf = b; Buffer::Destroy(buf); });
                storage = nullptr;
            }

            auto &storageDevice = m_storagesDevice[i];
            if (storageDevice)
            {
                RHII.AddToDeletionQueue([b = storageDevice]()
                                        { Buffer* buf = b; Buffer::Destroy(buf); });
                storageDevice = nullptr;
            }

            storage = Buffer::Create({
                .size = storageSize,
                .usage = storageUsage,
                // Per-node matrix table is CPU-written every frame and read by the GPU culling/depth/
                // GBuffer passes. Keep it device-local (ReBAR / DX12 GPU upload heap) so the culling
                // pass reads matrices from VRAM instead of paying a cold per-frame PCIe read of all N
                // matrices (the DX12 CullingPass cost driver vs Vulkan, which already lands it in BAR).
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT_DEVICE,
                .name = "storage_Geometry_buffer_" + std::to_string(i),
            });

            if (useStorageDeviceMirror)
            {
                storageDevice = Buffer::Create({
                    .size = storageSize,
                    .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                    .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY,
                    .name = "storageDevice_Geometry_buffer_" + std::to_string(i),
                });
            }
        }
    }

    void Scene::MarkUniformsDirty()
    {
        for (uint32_t i = 0; i < GetNodeCount(); i++)
            m_nodeRuntime[i].dirtyUniforms = 0xFF;
    }

    void Scene::CreateIndirectBuffers(CommandBuffer *cmd)
    {
        uint32_t indirectCount = 0;
        std::vector<PeDrawIndexedIndirectCommand> indirectCommands;
        indirectCommands.reserve(m_meshCount);

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            const auto &refs = m_nodeComponentCache[i].meshRefs->meshRefs;
            m_nodeRuntime[i].meshRefIndirect.assign(refs.size(), UINT32_MAX);

            if (m_nodeRuntime[i].gpuPending || !IsNodeHierarchyEnabled(m_nodeIds[i]))
                continue;

            for (uint32_t slot = 0; slot < static_cast<uint32_t>(refs.size()); slot++)
            {
                int meshIdx = refs[slot];
                if (!IsValidMeshIndex(meshIdx))
                    continue;

                const Mesh &mesh = m_meshes[meshIdx];
                if (mesh.indexCount == 0 || !IsRasterIndirectMesh(mesh))
                    continue;

                m_nodeRuntime[i].meshRefIndirect[slot] = indirectCount;

                PeDrawIndexedIndirectCommand indirectCommand{};
                indirectCommand.indexCount = mesh.indexCount;
                indirectCommand.instanceCount = 1;
                indirectCommand.firstIndex = mesh.indexOffset;
                indirectCommand.vertexOffset = mesh.vertexOffset;
                indirectCommand.firstInstance = indirectCount;
                indirectCommands.push_back(indirectCommand);

                indirectCount++;
            }
        }

        PE_ERROR_IF(indirectCount != m_meshCount, "Scene::UploadBuffers: Indirect count mismatch!");

        m_indirectCapacity = 1;
        while (m_indirectCapacity < std::max(1u, indirectCount))
            m_indirectCapacity <<= 1;

        const uint32_t indirectBufferCount = std::max(1u, indirectCount);
        m_indirectAll = Buffer::Create({
            .size = indirectBufferCount * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
            // TRANSFER_SRC: ReserveArenaCapacity copies the existing draws out of this buffer when it
            // regrows for arena headroom (Vulkan validates copy-source usage; DX12 does not).
            .usage = PE_BUFFER_USAGE_INDIRECT_BUFFER | PE_BUFFER_USAGE_STORAGE_BUFFER |
                     PE_BUFFER_USAGE_VERTEX_BUFFER |
                     PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_TRANSFER_SRC,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = "indirect_Geometry_buffer_all",
        });
        if (indirectCount > 0)
            cmd->CopyBufferStaged(m_indirectAll, indirectCommands.data(), indirectCommands.size() * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE, 0);

        if (indirectCount > 0)
        {
            BufferBarrierInfo indirectBarrierInfo{};
            indirectBarrierInfo.buffer = m_indirectAll;
            indirectBarrierInfo.stageMask = PE_STAGE_DRAW_INDIRECT | PE_STAGE_COMPUTE_SHADER;
            indirectBarrierInfo.accessMask = PE_ACCESS_INDIRECT_COMMAND_READ | PE_ACCESS_SHADER_READ;
            indirectBarrierInfo.size = indirectCount * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
            indirectBarrierInfo.offset = 0;
            cmd->BufferBarrier(indirectBarrierInfo);
        }

        auto createFilteredIndirect = [&](const std::string &name)
        {
            std::vector<Buffer *> vec(RHII.GetSwapchainImageCount());
            for (uint32_t i = 0; i < vec.size(); ++i)
            {
                vec[i] = Buffer::Create({
                    .size = m_indirectCapacity * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
                    .usage = PE_BUFFER_USAGE_INDIRECT_BUFFER | PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                    .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                    .name = name + std::to_string(i),
                });
            }
            return vec;
        };

        m_cullingCountersBuffers.resize(RHII.GetSwapchainImageCount());
        for (uint32_t i = 0; i < m_cullingCountersBuffers.size(); ++i)
        {
            m_cullingCountersBuffers[i] = Buffer::Create({
                .size = 9 * sizeof(uint32_t), // 8 base buckets + terrain (index 8)
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_INDIRECT_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                .name = "culling_counters_" + std::to_string(i),
            });
        }

        // LOD params UBO (CullingCS binding 16): refilled each frame from SceneSettings in UpdateLodUniforms.
        m_lodUniforms.resize(RHII.GetSwapchainImageCount());
        for (uint32_t i = 0; i < m_lodUniforms.size(); ++i)
        {
            m_lodUniforms[i] = Buffer::Create({
                .size = RHII.AlignUniform(sizeof(LodUBOData)),
                .usage = PE_BUFFER_USAGE_UNIFORM_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
                .name = "lod_params_uniform_" + std::to_string(i),
            });
            m_lodUniforms[i]->Map();
            m_lodUniforms[i]->Zero();
            m_lodUniforms[i]->Flush();
            m_lodUniforms[i]->Unmap();
        }

        m_indirectOpaqueSS = createFilteredIndirect("indirect_OpaqueSS_");
        m_indirectAlphaCutSS = createFilteredIndirect("indirect_AlphaCutSS_");
        m_indirectOpaqueDS = createFilteredIndirect("indirect_OpaqueDS_");
        m_indirectAlphaCutDS = createFilteredIndirect("indirect_AlphaCutDS_");
        m_indirectAlphaBlend = createFilteredIndirect("indirect_AlphaBlend_");
        m_indirectTransmission = createFilteredIndirect("indirect_Transmission_");
        m_indirectSelected = createFilteredIndirect("indirect_Selected_");
        m_indirectVoxels = createFilteredIndirect("indirect_Voxels_");
        m_indirectTerrain = createFilteredIndirect("indirect_Terrain_");
        m_shadowIndirectRegular = createFilteredIndirect("shadow_indirect_regular_");
        m_shadowIndirectVoxels = createFilteredIndirect("shadow_indirect_voxels_");

        m_shadowCullCounters.resize(RHII.GetSwapchainImageCount());
        for (uint32_t i = 0; i < m_shadowCullCounters.size(); ++i)
        {
            m_shadowCullCounters[i] = Buffer::Create({
                .size = 2 * sizeof(uint32_t),
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_INDIRECT_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                .name = "shadow_cull_counters_" + std::to_string(i),
            });
        }

        auto createSortKeyBuffer = [&](const std::string &name)
        {
            std::vector<Buffer *> vec(RHII.GetSwapchainImageCount());
            for (uint32_t i = 0; i < vec.size(); ++i)
            {
                vec[i] = Buffer::Create({
                    .size = m_indirectCapacity * sizeof(float),
                    .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                    .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                    .name = name + std::to_string(i),
                });
            }
            return vec;
        };
        m_sortKeysAlphaBlend = createSortKeyBuffer("sortKeys_AlphaBlend_");
        m_sortKeysTransmission = createSortKeyBuffer("sortKeys_Transmission_");

        // --- Two-phase Hi-Z occlusion (opaque-only A/B indirect sets + per-set counters + a
        // persistent per-draw visibility flag). Recreated on every geometry/draw-index rebuild,
        // which re-seeds visibility (draw indices are reassigned here, so stale bits are invalid).
        auto createOccCounters = [&](const std::string &name)
        {
            std::vector<Buffer *> vec(RHII.GetSwapchainImageCount());
            for (uint32_t i = 0; i < vec.size(); ++i)
                vec[i] = Buffer::Create({
                    .size = 7 * sizeof(uint32_t),
                    .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_INDIRECT_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                    .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                    .name = name + std::to_string(i),
                });
            return vec;
        };
        m_occCountersA = createOccCounters("occ_countersA_");
        m_occCountersB = createOccCounters("occ_countersB_");
        m_occOpaqueSSA = createFilteredIndirect("occ_OpaqueSSA_");
        m_occAlphaCutSSA = createFilteredIndirect("occ_AlphaCutSSA_");
        m_occOpaqueDSA = createFilteredIndirect("occ_OpaqueDSA_");
        m_occAlphaCutDSA = createFilteredIndirect("occ_AlphaCutDSA_");
        m_occOpaqueSSB = createFilteredIndirect("occ_OpaqueSSB_");
        m_occAlphaCutSSB = createFilteredIndirect("occ_AlphaCutSSB_");
        m_occOpaqueDSB = createFilteredIndirect("occ_OpaqueDSB_");
        m_occAlphaCutDSB = createFilteredIndirect("occ_AlphaCutDSB_");

        // Persistent per-draw visibility (1 = visible last frame). Seed to 1 so the first frame
        // after a rebuild draws everything in phase 1 (full pyramid) and phase 2 finds nothing new.
        m_visibility = Buffer::Create({
            .size = m_indirectCapacity * sizeof(uint32_t),
            // TRANSFER_SRC: ReserveArenaCapacity copies the live visibility bits out when it regrows.
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_TRANSFER_SRC,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = "occ_visibility",
        });
        cmd->FillBuffer(m_visibility, 0, m_indirectCapacity * sizeof(uint32_t), 1u);
        {
            BufferBarrierInfo vb{};
            vb.buffer = m_visibility;
            vb.stageMask = PE_STAGE_COMPUTE_SHADER;
            vb.accessMask = PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE;
            vb.size = m_indirectCapacity * sizeof(uint32_t);
            vb.offset = 0;
            cmd->BufferBarrier(vb);
        }
    }

    void Scene::UpdateImageViews()
    {
        m_imageViews.clear();
        m_imageViews.reserve(m_imageStore.size());

        const auto &defaults = ModelAsset::GetDefaultResources();
        OrderedMap<Image *, uint32_t> imagesMap{};

        for (const ResourceHandle<Image> &image : m_imageStore)
        {
            auto insertResult = imagesMap.insert(image.get(), static_cast<uint32_t>(m_imageViews.size()));
            if (insertResult.first)
            {
                ImageView *srv = image->GetSRV();
                PE_ERROR_IF(!srv, "UpdateImageViews: image '%s' has no SRV", image->GetName().c_str());
                m_imageViews.push_back(srv ? srv : defaults.white->GetSRV());
            }
        }

        // Disabled pooled rigs can re-enable without a texture rebuild, so keep their cached
        // imageViewIndices stable across rebuilds.
        for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
        {
            for (int meshIndex : m_nodeComponentCache[ni].meshRefs->meshRefs)
            {
                if (!IsValidMeshIndex(meshIndex))
                    continue;

                const Mesh &mesh = m_meshes[meshIndex];
                MeshRuntime &rt = m_meshRuntimes[meshIndex];

                for (int k = 0; k < 5; k++)
                {
                    Image *image = nullptr;
                    if (mesh.materialInstance)
                        image = mesh.materialInstance->GetTexture(k);
                    else if (mesh.material)
                        image = mesh.material->textures[k].get();
                    bool isDefault = (image == defaults.black || image == defaults.white || image == defaults.normal);

                    if (image && !isDefault)
                    {
                        auto insertResult = imagesMap.insert(image, static_cast<uint32_t>(m_imageViews.size()));
                        if (insertResult.first)
                        {
                            ImageView *srv = image->GetSRV();
                            PE_ERROR_IF(!srv, "UpdateImageViews: image '%s' has no SRV", image->GetName().c_str());
                            m_imageViews.push_back(srv ? srv : defaults.white->GetSRV());
                        }
                        rt.imageViewIndices[k] = imagesMap[image];
                    }
                    else
                    {
                        rt.imageViewIndices[k] = 0xFFFFFFFF;
                    }
                }

                Material *mat = mesh.material;
                if (mat)
                {
                    mat->namedTextureIndices.clear();
                    for (const auto &[name, imgHandle] : mat->namedTextures)
                    {
                        Image *image = imgHandle.get();
                        if (!image)
                        {
                            mat->namedTextureIndices[name] = 0xFFFFFFFF;
                            continue;
                        }

                        auto insertResult = imagesMap.insert(image, static_cast<uint32_t>(m_imageViews.size()));
                        if (insertResult.first)
                        {
                            ImageView *srv = image->GetSRV();
                            PE_ERROR_IF(!srv, "UpdateImageViews: named texture '%s' has no SRV", name.c_str());
                            m_imageViews.push_back(srv ? srv : defaults.white->GetSRV());
                        }
                        mat->namedTextureIndices[name] = imagesMap[image];
                    }

                    MaterialInstance *inst = mesh.materialInstance;
                    if (inst)
                    {
                        inst->namedTextureIndices = mat->namedTextureIndices;
                        for (const auto &[name, imgHandle] : inst->GetNamedTextureOverrides())
                        {
                            Image *image = imgHandle.get();
                            if (!image)
                            {
                                inst->namedTextureIndices[name] = 0xFFFFFFFF;
                                continue;
                            }

                            auto insertResult = imagesMap.insert(image, static_cast<uint32_t>(m_imageViews.size()));
                            if (insertResult.first)
                            {
                                ImageView *srv = image->GetSRV();
                                PE_ERROR_IF(!srv, "UpdateImageViews: instance named texture '%s' has no SRV", name.c_str());
                                m_imageViews.push_back(srv ? srv : defaults.white->GetSRV());
                            }
                            inst->namedTextureIndices[name] = imagesMap[image];
                        }
                    }
                }
            }
        }

        m_geometryVersion++;
    }

    void Scene::CreateMaterialTable()
    {
        auto deferDestroy = [](Buffer *&buffer)
        {
            if (!buffer)
                return;
            RHII.AddToDeletionQueue([b = buffer]()
                                    { Buffer *old = b; Buffer::Destroy(old); });
            buffer = nullptr;
        };

        deferDestroy(m_materialTable);

        std::vector<MaterialGpuData> tableData;

        for (auto &mat : m_ownedMaterials)
        {
            mat->gpuIndex = 0xFFFFFFFF;
            mat->gpuByteOffset = 0xFFFFFFFF;
        }
        for (auto *model : m_models)
        {
            for (auto &mat : model->GetOwnedMaterials())
            {
                mat->gpuIndex = 0xFFFFFFFF;
                mat->gpuByteOffset = 0xFFFFFFFF;
            }
        }

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (!IsNodeHierarchyEnabled(m_nodeIds[i]))
                continue;

            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (!IsValidMeshIndex(meshIdx))
                    continue;

                Mesh &mesh = m_meshes[meshIdx];
                if (mesh.indexCount == 0)
                    continue;

                MeshRuntime &meshRt = m_meshRuntimes[meshIdx];

                if (mesh.materialInstance)
                {
                    meshRt.materialGpuIndex = static_cast<uint32_t>(tableData.size());
                    tableData.push_back(mesh.materialInstance->BuildGpuData());
                }
                else if (mesh.material)
                {
                    if (mesh.material->gpuIndex == 0xFFFFFFFF)
                    {
                        mesh.material->gpuIndex = static_cast<uint32_t>(tableData.size());
                        tableData.push_back(mesh.material->BuildGpuData());
                    }
                    meshRt.materialGpuIndex = mesh.material->gpuIndex;
                }
            }
        }

        if (tableData.empty())
        {
            MaterialGpuData defaultMat{};
            defaultMat.baseColorFactor = vec4(1.f);
            defaultMat.emissiveTransmission = vec4(0.f);
            defaultMat.pbrParams = vec4(0.f, 1.f, 0.5f, 1.f);
            tableData.push_back(defaultMat);
        }

        m_materialTable = Buffer::Create({
            .size = tableData.size() * sizeof(MaterialGpuData),
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
            .name = "Scene_materialTable",
        });

        m_materialTable->Map();
        BufferRange range{};
        range.data = tableData.data();
        range.offset = 0;
        range.size = tableData.size() * sizeof(MaterialGpuData);
        m_materialTable->Copy(1, &range, true);
        m_materialTable->Flush(range.size, 0);
        m_materialTable->Unmap();

        deferDestroy(m_materialByteBuffer);
        m_materialByteBufferUsed = 0;

        struct ByteEntry
        {
            uint32_t *offsetDst;
            uint32_t *sizeDst;
            std::vector<uint8_t> data;
        };
        std::vector<ByteEntry> byteEntries;
        uint32_t totalBytes = 0;

        std::unordered_map<std::string, MaterialLayout> layoutCache;

        for (uint32_t i = 0; i < GetNodeCount(); i++)
            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
                if (IsValidMeshIndex(meshIdx) && m_meshes[meshIdx].materialInstance)
                {
                    m_meshes[meshIdx].materialInstance->gpuByteOffset = 0xFFFFFFFF;
                    m_meshes[meshIdx].materialInstance->gpuByteSize = 0;
                }

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (!IsNodeHierarchyEnabled(m_nodeIds[i]))
                continue;

            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (!IsValidMeshIndex(meshIdx))
                    continue;
                Mesh &mesh = m_meshes[meshIdx];
                if (mesh.indexCount == 0 || !mesh.material || !mesh.material->passInfoAsset)
                    continue;

                Material *mat = mesh.material;
                std::string passId = mat->passInfoAsset->GetResourceId();

                auto cacheIt = layoutCache.find(passId);
                if (cacheIt == layoutCache.end())
                {
                    cacheIt = layoutCache.emplace(passId, ReflectMaterialLayout(*mat->passInfoAsset)).first;
                }
                const MaterialLayout &layout = cacheIt->second;

                mat->cachedLayout = layout;

                if (!layout.valid || layout.structMembers.empty())
                    continue;

                if (mesh.materialInstance)
                {
                    MaterialInstance *inst = mesh.materialInstance;
                    if (inst->gpuByteOffset != 0xFFFFFFFF)
                        continue;

                    std::vector<uint8_t> byteData = inst->BuildByteAddressData(layout.structMembers, layout.textureSlots, layout.totalByteSize);
                    if (byteData.empty())
                        continue;

                    inst->gpuByteSize = static_cast<uint32_t>(byteData.size());
                    inst->gpuByteOffset = totalBytes;
                    totalBytes += (static_cast<uint32_t>(byteData.size()) + 3u) & ~3u;
                    byteEntries.push_back({&inst->gpuByteOffset, &inst->gpuByteSize, std::move(byteData)});
                }
                else
                {
                    if (mat->gpuByteOffset != 0xFFFFFFFF)
                        continue;

                    std::vector<uint8_t> byteData = mat->BuildByteAddressData(layout.structMembers, layout.textureSlots, layout.totalByteSize);
                    if (byteData.empty())
                        continue;

                    mat->gpuByteSize = static_cast<uint32_t>(byteData.size());
                    mat->gpuByteOffset = totalBytes;
                    totalBytes += (static_cast<uint32_t>(byteData.size()) + 3u) & ~3u;
                    byteEntries.push_back({&mat->gpuByteOffset, &mat->gpuByteSize, std::move(byteData)});
                }
            }
        }

        if (totalBytes == 0)
            totalBytes = 4;

        m_materialByteBuffer = Buffer::Create({
            .size = totalBytes,
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
            .name = "Scene_materialByteBuffer",
        });

        if (!byteEntries.empty())
        {
            m_materialByteBuffer->Map();
            for (const auto &entry : byteEntries)
            {
                BufferRange br{};
                br.data = const_cast<uint8_t *>(entry.data.data());
                br.offset = *entry.offsetDst;
                br.size = entry.data.size();
                m_materialByteBuffer->Copy(1, &br, true);
            }
            m_materialByteBuffer->Flush();
            m_materialByteBuffer->Unmap();
        }
        m_materialByteBufferUsed = totalBytes;
    }

    bool Scene::UpdateDirtyMaterials()
    {
        if (!m_materialTable)
        {
            m_materialDirty = false;
            return false;
        }

        if (m_texturesDirty)
        {
            m_materialDirty = true;
            return true;
        }

        bool anyDirty = false;
        bool pendingDirty = false;

        auto uploadMaterialGpuData = [&](uint32_t gpuIndex, MaterialGpuData data)
        {
            if (gpuIndex == 0xFFFFFFFF)
                return;

            m_materialTable->Map();
            BufferRange range{};
            range.data = &data;
            range.offset = gpuIndex * sizeof(MaterialGpuData);
            range.size = sizeof(MaterialGpuData);
            m_materialTable->Copy(1, &range, true);
            m_materialTable->Flush(range.size, range.offset);
            m_materialTable->Unmap();
            anyDirty = true;
        };

        auto updateIfDirty = [&](Material *mat)
        {
            if (!mat->dirty || mat->gpuIndex == 0xFFFFFFFF)
                return;

            uploadMaterialGpuData(mat->gpuIndex, mat->BuildGpuData());
            // Don't clear dirty here — collectByteIfDirty still needs it
        };

        for (auto &mat : m_ownedMaterials)
            updateIfDirty(mat.get());
        for (auto *model : m_models)
            for (auto &mat : model->GetOwnedMaterials())
                updateIfDirty(mat.get());

        for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
        {
            if (!IsNodeHierarchyEnabled(m_nodeIds[ni]))
                continue;

            if (m_nodeRuntime[ni].gpuPending)
            {
                pendingDirty = true;
                continue;
            }

            for (int meshIdx : m_nodeComponentCache[ni].meshRefs->meshRefs)
            {
                if (!IsValidMeshIndex(meshIdx))
                    continue;

                Mesh &mesh = m_meshes[meshIdx];
                MaterialInstance *inst = mesh.materialInstance;
                if (!inst || !inst->dirty)
                    continue;

                const uint32_t materialGpuIndex = m_meshRuntimes[meshIdx].materialGpuIndex;
                if (materialGpuIndex == 0xFFFFFFFF)
                {
                    pendingDirty = true;
                    continue;
                }

                uploadMaterialGpuData(materialGpuIndex, inst->BuildGpuData());
            }
        }

        struct ByteDirtyEntry
        {
            uint32_t offset;
            bool *dirtyFlag;
            std::vector<uint8_t> data;
        };
        std::vector<ByteDirtyEntry> byteDirtyEntries;

        auto collectByteIfDirty = [&](Material *mat)
        {
            if (!mat->dirty)
                return;

            // Standard PBR materials only need the structured material table upload above.
            if (!mat->passInfoAsset)
            {
                mat->dirty = false;
                return;
            }
            if (mat->gpuByteOffset == 0xFFFFFFFF || !mat->cachedLayout.valid)
            {
                pendingDirty = true;
                return;
            }
            if (mat->cachedLayout.structMembers.empty())
            {
                mat->dirty = false;
                return;
            }

            std::vector<uint8_t> byteData = mat->BuildByteAddressData(
                mat->cachedLayout.structMembers, mat->cachedLayout.textureSlots, mat->cachedLayout.totalByteSize);
            if (byteData.empty())
            {
                mat->dirty = false;
                return;
            }

            byteDirtyEntries.push_back({mat->gpuByteOffset, &mat->dirty, std::move(byteData)});
        };

        for (auto &mat : m_ownedMaterials)
            collectByteIfDirty(mat.get());
        for (auto *model : m_models)
            for (auto &mat : model->GetOwnedMaterials())
                collectByteIfDirty(mat.get());

        for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
        {
            if (!IsNodeHierarchyEnabled(m_nodeIds[ni]))
                continue;

            if (m_nodeRuntime[ni].gpuPending)
            {
                pendingDirty = true;
                continue;
            }

            for (int meshIdx : m_nodeComponentCache[ni].meshRefs->meshRefs)
            {
                if (!IsValidMeshIndex(meshIdx))
                    continue;
                Mesh &mesh = m_meshes[meshIdx];
                MaterialInstance *inst = mesh.materialInstance;
                if (!inst || !inst->dirty)
                    continue;

                Material *parent = inst->GetParent();
                if (!parent)
                {
                    inst->dirty = false;
                    continue;
                }
                // Standard PBR instances only need the structured material table upload above.
                if (!parent->passInfoAsset)
                {
                    inst->dirty = false;
                    continue;
                }
                if (inst->gpuByteOffset == 0xFFFFFFFF || !parent->cachedLayout.valid)
                {
                    pendingDirty = true;
                    continue;
                }
                if (parent->cachedLayout.structMembers.empty())
                {
                    inst->dirty = false;
                    continue;
                }

                std::vector<uint8_t> byteData = inst->BuildByteAddressData(
                    parent->cachedLayout.structMembers, parent->cachedLayout.textureSlots, parent->cachedLayout.totalByteSize);
                if (byteData.empty())
                {
                    inst->dirty = false;
                    continue;
                }

                byteDirtyEntries.push_back({inst->gpuByteOffset, &inst->dirty, std::move(byteData)});
            }
        }

        if (!byteDirtyEntries.empty() && m_materialByteBuffer)
        {
            m_materialByteBuffer->Map();
            for (auto &entry : byteDirtyEntries)
            {
                BufferRange br{};
                br.data = const_cast<uint8_t *>(entry.data.data());
                br.offset = entry.offset;
                br.size = entry.data.size();
                m_materialByteBuffer->Copy(1, &br, true);
                *entry.dirtyFlag = false;
            }
            m_materialByteBuffer->Flush(m_materialByteBufferUsed, 0);
            m_materialByteBuffer->Unmap();
            anyDirty = true;
        }
        else if (!byteDirtyEntries.empty())
        {
            pendingDirty = true;
        }

        if (anyDirty)
            m_geometryVersion++;

        m_materialDirty = pendingDirty;
        return pendingDirty;
    }

    void Scene::CreateMeshConstants(CommandBuffer *cmd)
    {
        // Defer the old buffers' destruction: UploadBuffers can be driven OUTSIDE the render loop's
        // WaitAllFramesCommands guard (e.g. VoxelWorld::Create -> FlushPendingGpuWork), so an immediate
        // Buffer::Destroy here would free meshConstants while a prior frame's command buffer (GBuffer /
        // Depth / SelectionOutline bind it) is still in flight -> VUID-vkDestroyBuffer-buffer-00922. The
        // deletion queue frees only after the owning frame's fence signals (matches ReserveArenaCapacity).
        if (m_meshConstants)
            RHII.AddToDeletionQueue([b = m_meshConstants]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
        if (m_meshConstantsDevice)
            RHII.AddToDeletionQueue([b = m_meshConstantsDevice]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
        const size_t meshConstantsCapacity = std::max<size_t>(1, m_meshCount);
        const size_t meshConstantsSize = meshConstantsCapacity * sizeof(Mesh_Constants);
        const bool useMeshConstantsMirror = RHII.GetApi() == PE_GRAPHICS_API_DX12;
        m_meshConstants = Buffer::Create({
            .size = meshConstantsSize,
            // TRANSFER_SRC: copy source for the DX12 device mirror below AND for ReserveArenaCapacity's
            // regrow copy (Vulkan validates copy-source usage, so it must be set on both backends).
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_TRANSFER_SRC,
            // CPU-written on geometry rebuild, read by the GPU culling/depth/GBuffer passes. On DX12
            // this is only the staging source — the GPU reads the cached DEFAULT m_meshConstantsDevice
            // mirror instead, because uncached GPU_UPLOAD reads dominate the cull pass (~0.6 ms @ 50k).
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT_DEVICE,
            .name = "Scene_meshConstants",
        });

        if (useMeshConstantsMirror)
        {
            m_meshConstantsDevice = Buffer::Create({
                .size = meshConstantsSize,
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY,
                .name = "Scene_meshConstantsDevice",
            });
        }

        size_t offset = 0;
        m_hasTransparentMeshes = false;
        m_hasAlphaBlendMeshes = false;
        m_hasTransmissionMeshes = false;
        m_hasLinesMeshes = false;
        m_alphaBlendMeshCount = 0;
        m_transmissionMeshCount = 0;
        m_meshConstants->Map();
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending || !IsNodeHierarchyEnabled(m_nodeIds[i]))
                continue;

            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (!IsValidMeshIndex(meshIdx))
                    continue;

                const Mesh &mesh = m_meshes[meshIdx];
                if (mesh.indexCount == 0)
                    continue;

                if (mesh.renderType == RenderType::Lines || mesh.renderType == RenderType::SpriteOutline)
                {
                    m_hasLinesMeshes = true;
                    continue;
                }
                else if (mesh.renderType == RenderType::AlphaBlend)
                {
                    m_hasAlphaBlendMeshes = true;
                    m_alphaBlendMeshCount++;
                }
                else if (mesh.renderType == RenderType::Transmission)
                {
                    m_hasTransmissionMeshes = true;
                    m_transmissionMeshCount++;
                }
                m_hasTransparentMeshes = m_hasAlphaBlendMeshes || m_hasTransmissionMeshes;

                Mesh_Constants constants = ComputeMeshConstants(i, meshIdx);

                BufferRange range{};
                range.data = &constants;
                range.offset = offset;
                range.size = sizeof(Mesh_Constants);
                m_meshConstants->Copy(1, &range, true);

                offset += sizeof(Mesh_Constants);
            }
        }
        m_meshConstants->Flush(offset, 0);
        m_meshConstants->Unmap();

        // DX12: publish the CPU-written constants into the GPU-cached DEFAULT mirror. Geometry rebuilds
        // run with frames idle (WaitAllFramesCommands), and the buffer is read-only afterwards, so a
        // single (unringed) device buffer copied once here is safe; the cull/depth/GBuffer passes then
        // read cached VRAM instead of the slow GPU_UPLOAD heap.
        if (m_meshConstantsDevice)
        {
            cmd->CopyBuffer(m_meshConstants, m_meshConstantsDevice, m_meshConstants->Size(), 0, 0);
            BufferBarrierInfo barrier{};
            barrier.buffer = m_meshConstantsDevice;
            barrier.stageMask = PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_SHADER;
            barrier.accessMask = PE_ACCESS_SHADER_STORAGE_READ;
            barrier.offset = 0;
            barrier.size = m_meshConstants->Size();
            cmd->BufferBarrier(barrier);
            m_meshConstantsDevice->GetTrackInfo() = barrier;
        }
        // The mirror was just recreated; force UpdateMeshSelectionFlags to republish the selection next
        // frame regardless of whether the selected set changed (ComputeMeshConstants doesn't bake it).
        m_meshSelectionMirrorSignature = ~0ull;
        m_pendingTextureMeshUploads.clear();
    }

    void Scene::DestroyBuffers()
    {
        if (m_buffer)
        {
            RHII.AddToDeletionQueue([b = m_buffer]()
                                    { Buffer* buf = b; Buffer::Destroy(buf); });
            m_buffer = nullptr;
        }

        for (auto &storage : m_storages)
        {
            if (storage)
            {
                RHII.AddToDeletionQueue([b = storage]()
                                        { Buffer* buf = b; Buffer::Destroy(buf); });
                storage = nullptr;
            }
        }

        for (auto &storageDevice : m_storagesDevice)
        {
            if (storageDevice)
            {
                RHII.AddToDeletionQueue([b = storageDevice]()
                                        { Buffer* buf = b; Buffer::Destroy(buf); });
                storageDevice = nullptr;
            }
        }

        auto destroyBufferVec = [](std::vector<Buffer *> &vec)
        {
            for (auto &buf : vec)
            {
                if (buf)
                {
                    RHII.AddToDeletionQueue([b = buf]()
                                            { Buffer *fb = b; Buffer::Destroy(fb); });
                    buf = nullptr;
                }
            }
        };

        destroyBufferVec(m_cullingCountersBuffers);
        destroyBufferVec(m_lodUniforms);
        destroyBufferVec(m_indirectOpaqueSS);
        destroyBufferVec(m_indirectAlphaCutSS);
        destroyBufferVec(m_indirectOpaqueDS);
        destroyBufferVec(m_indirectAlphaCutDS);
        destroyBufferVec(m_indirectAlphaBlend);
        destroyBufferVec(m_indirectTransmission);
        destroyBufferVec(m_indirectSelected);
        destroyBufferVec(m_indirectVoxels);
        destroyBufferVec(m_indirectTerrain);
        destroyBufferVec(m_shadowIndirectRegular);
        destroyBufferVec(m_shadowIndirectVoxels);
        destroyBufferVec(m_shadowCullCounters);
        destroyBufferVec(m_sortKeysAlphaBlend);
        destroyBufferVec(m_sortKeysTransmission);

        // Two-phase Hi-Z occlusion A/B sets + counters + persistent visibility.
        destroyBufferVec(m_occCountersA);
        destroyBufferVec(m_occCountersB);
        destroyBufferVec(m_occOpaqueSSA);
        destroyBufferVec(m_occAlphaCutSSA);
        destroyBufferVec(m_occOpaqueDSA);
        destroyBufferVec(m_occAlphaCutDSA);
        destroyBufferVec(m_occOpaqueSSB);
        destroyBufferVec(m_occAlphaCutSSB);
        destroyBufferVec(m_occOpaqueDSB);
        destroyBufferVec(m_occAlphaCutDSB);
        if (m_visibility)
        {
            RHII.AddToDeletionQueue([b = m_visibility]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
            m_visibility = nullptr;
        }

        if (m_indirectAll)
        {
            RHII.AddToDeletionQueue([b = m_indirectAll]()
                                    { Buffer* buf = b; Buffer::Destroy(buf); });
            m_indirectAll = nullptr;
        }

        if (m_materialTable)
        {
            RHII.AddToDeletionQueue([b = m_materialTable]()
                                    { Buffer* buf = b; Buffer::Destroy(buf); });
            m_materialTable = nullptr;
        }

        if (m_materialByteBuffer)
        {
            RHII.AddToDeletionQueue([b = m_materialByteBuffer]()
                                    { Buffer* buf = b; Buffer::Destroy(buf); });
            m_materialByteBuffer = nullptr;
            m_materialByteBufferUsed = 0;
        }

        // The combined geometry buffer (and the voxel-arena tail living inside it) was just destroyed.
        // Clear the arena bookkeeping so nothing writes into the dead region before the voxel world
        // re-reserves it: AddArenaMesh's capacity guard now bails, and HasArenaVoxels() goes false so
        // the voxel teardown skips its arena flush. Fixes "CopyBufferStaged: dst range overflow" on a
        // geometry reload (e.g. play-stop / snapshot restore) while voxels were live. ReserveArenaCapacity
        // re-publishes these after its own regrow, so the grow-and-preserve path is unaffected.
        // The dedicated voxel buffers die with the arena (geometry teardown / rebuild). HasArenaVoxels()
        // goes false here so no draw binds them after this; ReserveArenaCapacity re-creates them.
        if (m_voxelVertexBuf)
            RHII.AddToDeletionQueue([b = m_voxelVertexBuf]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
        if (m_voxelIndexBuf)
            RHII.AddToDeletionQueue([b = m_voxelIndexBuf]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
        m_voxelVertexBuf = nullptr;
        m_voxelIndexBuf = nullptr;

        m_arenaSlots.clear();
        m_arenaFreeSlots.clear();
        m_arenaSlotBase = 0;
        m_arenaVertexBase = 0;
        m_arenaVertexCapacity = 0;
        m_arenaVertexUsed = 0;
        m_arenaIdxByteBase = 0;
        m_arenaIdxCapacity = 0;
        m_arenaIdxUsed = 0;
    }
    void Scene::RebuildRasterInstances(CommandBuffer *cmd)
    {
        for (auto &storage : m_storages)
        {
            if (storage)
            {
                RHII.AddToDeletionQueue([b = storage]()
                                        { Buffer *buf = b; Buffer::Destroy(buf); });
                storage = nullptr;
            }
        }
        for (auto &storageDevice : m_storagesDevice)
        {
            if (storageDevice)
            {
                RHII.AddToDeletionQueue([b = storageDevice]()
                                        { Buffer *buf = b; Buffer::Destroy(buf); });
                storageDevice = nullptr;
            }
        }
        auto destroyBufferVecEager = [](std::vector<Buffer *> &vec)
        {
            for (auto &buf : vec)
            {
                if (buf)
                {
                    RHII.AddToDeletionQueue([b = buf]()
                                            { Buffer *fb = b; Buffer::Destroy(fb); });
                    buf = nullptr;
                }
            }
        };

        destroyBufferVecEager(m_cullingCountersBuffers);
        destroyBufferVecEager(m_lodUniforms);
        destroyBufferVecEager(m_indirectOpaqueSS);
        destroyBufferVecEager(m_indirectAlphaCutSS);
        destroyBufferVecEager(m_indirectOpaqueDS);
        destroyBufferVecEager(m_indirectAlphaCutDS);
        destroyBufferVecEager(m_indirectAlphaBlend);
        destroyBufferVecEager(m_indirectTransmission);
        destroyBufferVecEager(m_indirectSelected);
        destroyBufferVecEager(m_indirectVoxels);
        destroyBufferVecEager(m_indirectTerrain);
        destroyBufferVecEager(m_shadowIndirectRegular);
        destroyBufferVecEager(m_shadowIndirectVoxels);
        destroyBufferVecEager(m_shadowCullCounters);
        destroyBufferVecEager(m_sortKeysAlphaBlend);
        destroyBufferVecEager(m_sortKeysTransmission);
        // Two-phase occlusion A/B sets + the persistent visibility flag are recreated by
        // CreateIndirectBuffers below; destroy the previous generation here or they leak on every
        // instance-only rebuild (mirrors the DestroyBuffers teardown).
        destroyBufferVecEager(m_occCountersA);
        destroyBufferVecEager(m_occCountersB);
        destroyBufferVecEager(m_occOpaqueSSA);
        destroyBufferVecEager(m_occAlphaCutSSA);
        destroyBufferVecEager(m_occOpaqueDSA);
        destroyBufferVecEager(m_occAlphaCutDSA);
        destroyBufferVecEager(m_occOpaqueSSB);
        destroyBufferVecEager(m_occAlphaCutSSB);
        destroyBufferVecEager(m_occOpaqueDSB);
        destroyBufferVecEager(m_occAlphaCutDSB);
        if (m_visibility)
        {
            RHII.AddToDeletionQueue([b = m_visibility]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
            m_visibility = nullptr;
        }
        if (m_indirectAll)
        {
            RHII.AddToDeletionQueue([b = m_indirectAll]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
            m_indirectAll = nullptr;
        }

        m_meshCount = 0;
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending || !IsNodeHierarchyEnabled(m_nodeIds[i]))
                continue;
            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (IsValidMeshIndex(meshIdx) &&
                    m_meshes[meshIdx].indexCount > 0 &&
                    IsRasterIndirectMesh(m_meshes[meshIdx]))
                {
                    m_meshCount++;
                }
            }
        }

        CreateStorageBuffers();
        MarkUniformsDirty();
        CreateIndirectBuffers(cmd);
        UpdateImageViews();
        CreateMaterialTable();
        CreateMeshConstants(cmd);
    }

    int Scene::ReserveArenaCapacity(uint32_t vtxHeadroomBytes, uint32_t idxHeadroomBytes,
                                    uint32_t posUvHeadroomBytes, uint32_t extraDrawCapacity)
    {
        if (!m_buffer)
            return -1;
        (void)posUvHeadroomBytes; // voxel verts are packed (single 8 B stream) — no separate posUv stream

        // Voxel arena geometry lives in DEDICATED voxel-owned buffers, NOT the shared m_buffer. This
        // leaves m_buffer + the regular-mesh dual-stream layout untouched, removes the fragile re-lay-out
        // path (the home of the spike's "invisible voxel" bugs), and lets voxel geometry be packed/grown
        // independently. Verts are packed (8 B); the same buffer feeds the voxel GBuffer draw and the
        // voxel shadow draw (each VS unpacks what it needs). Indices get their own voxel buffer too, so
        // the arena never reshapes m_buffer.
        const size_t vtxStride = sizeof(VoxelVertex);
        const size_t idxStride = sizeof(uint16_t);
        const uint32_t arenaVertCap = static_cast<uint32_t>(vtxHeadroomBytes / vtxStride);
        const uint32_t arenaIdxCap = static_cast<uint32_t>((idxHeadroomBytes + idxStride - 1) / idxStride);

        Queue *q = RHII.GetMainQueue();
        q->WaitIdle();

        // Free any prior voxel buffers (a re-Init) through the deletion queue.
        if (m_voxelVertexBuf)
            RHII.AddToDeletionQueue([b = m_voxelVertexBuf]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
        if (m_voxelIndexBuf)
            RHII.AddToDeletionQueue([b = m_voxelIndexBuf]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });

        // TRANSFER_SRC so GrowArenaVoxelCapacity can copy the live geometry into a larger buffer.
        m_voxelVertexBuf = Buffer::Create({
            .size = std::max<size_t>(vtxStride, static_cast<size_t>(arenaVertCap) * vtxStride),
            .usage = PE_BUFFER_USAGE_VERTEX_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_TRANSFER_SRC,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = "voxel_vertex_buffer",
        });
        m_voxelIndexBuf = Buffer::Create({
            .size = std::max<size_t>(idxStride, static_cast<size_t>(arenaIdxCap) * idxStride),
            .usage = PE_BUFFER_USAGE_INDEX_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_TRANSFER_SRC,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = "voxel_index_buffer",
        });

        // Arena bookkeeping: offsets are base-0 into the dedicated voxel buffers.
        m_arenaVertexBase = 0;                // first arena vertex index (packed voxel stream)
        m_arenaVertexCapacity = arenaVertCap; // in vertices
        m_arenaVertexUsed = 0;
        m_arenaIdxByteBase = 0; // base-0 into the voxel index buffer
        m_arenaIdxCapacity = static_cast<size_t>(arenaIdxCap) * idxStride;
        m_arenaIdxUsed = 0;
        // Arena meshes occupy the TAIL slots [m_arenaSlotBase, m_meshCount). Regular meshes never
        // move (and MUST NOT trigger a geometry-dirty rebuild after this point — that destroys the
        // arena). The CPU shadow grows/shrinks with AddArenaMesh/RemoveArenaMesh.
        m_arenaSlotBase = m_meshCount;
        m_arenaSlots.clear();
        m_arenaFreeSlots.clear();

        if (extraDrawCapacity > 0)
        {
            const uint32_t needed = m_meshCount + extraDrawCapacity;
            uint32_t newCap = std::max(1u, m_indirectCapacity);
            while (newCap < needed)
                newCap <<= 1;

            // m_indirectAll is created EXACT-sized to m_meshCount by CreateIndirectBuffers (it is
            // not m_indirectCapacity-padded like the filtered/occlusion buffers). So it must ALWAYS
            // be regrown to newCap here, even when newCap == m_indirectCapacity (the common case when
            // the scene already has enough draws that pow2(meshCount) already covers the new draws —
            // m_indirectCapacity is then unchanged, but m_indirectAll is still only meshCount*stride
            // and an append at slot meshCount would overrun it). The filtered/occlusion/visibility/
            // mesh-constants buffers are m_indirectCapacity-sized and only need regrowth when newCap
            // actually exceeds the old capacity.
            {
                Buffer *newIndAll = Buffer::Create({
                    .size = newCap * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
                    .usage = PE_BUFFER_USAGE_INDIRECT_BUFFER |
                             PE_BUFFER_USAGE_STORAGE_BUFFER |
                             PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_TRANSFER_SRC,
                    .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                    .name = "indirect_Geometry_buffer_all",
                });
                if (m_indirectAll && m_meshCount > 0)
                {
                    CommandBuffer *c = q->AcquireCommandBuffer();
                    c->Begin();
                    c->CopyBuffer(m_indirectAll, newIndAll,
                                  m_meshCount * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE, 0, 0);
                    c->End();
                    q->Submit(1, &c, nullptr, nullptr);
                    c->Wait();
                    c->Return();
                }
                RHII.AddToDeletionQueue(
                    [b = m_indirectAll]()
                    { Buffer *buf = b; Buffer::Destroy(buf); });
                m_indirectAll = newIndAll;
            }

            if (newCap > m_indirectCapacity)
            {
                const uint32_t swapCount = static_cast<uint32_t>(m_indirectOpaqueSS.size());

                // Lambda: grow a per-swapchain-image indirect buffer vector (capacity-scaled)
                auto growIndirectVec = [&](std::vector<Buffer *> &vec, const std::string &name,
                                           PeBufferUsageFlags usage, size_t elementSize)
                {
                    for (uint32_t i = 0; i < swapCount; ++i)
                    {
                        Buffer *nb = Buffer::Create({
                            .size = newCap * elementSize,
                            .usage = usage,
                            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                            .name = name + std::to_string(i),
                        });
                        RHII.AddToDeletionQueue(
                            [b = vec[i]]()
                            { Buffer *buf = b; Buffer::Destroy(buf); });
                        vec[i] = nb;
                    }
                };

                const PeBufferUsageFlags indFlags = PE_BUFFER_USAGE_INDIRECT_BUFFER |
                                                    PE_BUFFER_USAGE_STORAGE_BUFFER |
                                                    PE_BUFFER_USAGE_TRANSFER_DST;
                const PeBufferUsageFlags sortFlags =
                    PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST;

                growIndirectVec(m_indirectOpaqueSS, "indirect_OpaqueSS_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_indirectAlphaCutSS, "indirect_AlphaCutSS_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_indirectOpaqueDS, "indirect_OpaqueDS_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_indirectAlphaCutDS, "indirect_AlphaCutDS_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_indirectAlphaBlend, "indirect_AlphaBlend_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_indirectTransmission, "indirect_Transmission_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_indirectSelected, "indirect_Selected_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_indirectVoxels, "indirect_Voxels_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_indirectTerrain, "indirect_Terrain_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_shadowIndirectRegular, "shadow_indirect_regular_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_shadowIndirectVoxels, "shadow_indirect_voxels_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_sortKeysAlphaBlend, "sortKeys_AlphaBlend_", sortFlags,
                                sizeof(float));
                growIndirectVec(m_sortKeysTransmission, "sortKeys_Transmission_", sortFlags,
                                sizeof(float));
                growIndirectVec(m_occOpaqueSSA, "occ_OpaqueSSA_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_occAlphaCutSSA, "occ_AlphaCutSSA_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_occOpaqueDSA, "occ_OpaqueDSA_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_occAlphaCutDSA, "occ_AlphaCutDSA_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_occOpaqueSSB, "occ_OpaqueSSB_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_occAlphaCutSSB, "occ_AlphaCutSSB_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_occOpaqueDSB, "occ_OpaqueDSB_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                growIndirectVec(m_occAlphaCutDSB, "occ_AlphaCutDSB_", indFlags,
                                PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);

                // Grow visibility buffer (copy existing bits, fill remainder with 1)
                {
                    Buffer *newVis = Buffer::Create({
                        .size = newCap * sizeof(uint32_t),
                        .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                        .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                        .name = "occ_visibility",
                    });
                    CommandBuffer *c = q->AcquireCommandBuffer();
                    c->Begin();
                    if (m_visibility && m_meshCount > 0)
                        c->CopyBuffer(m_visibility, newVis, m_meshCount * sizeof(uint32_t), 0, 0);
                    if (newCap > m_meshCount)
                        c->FillBuffer(newVis, m_meshCount * sizeof(uint32_t),
                                      (newCap - m_meshCount) * sizeof(uint32_t), 1u);
                    c->End();
                    q->Submit(1, &c, nullptr, nullptr);
                    c->Wait();
                    c->Return();
                    RHII.AddToDeletionQueue(
                        [b = m_visibility]()
                        { Buffer *buf = b; Buffer::Destroy(buf); });
                    m_visibility = newVis;
                }

                m_indirectCapacity = newCap;
            }

            // Grow mesh constants (+ DX12 device mirror) UNCONDITIONALLY to newCap. Like
            // m_indirectAll, these are created EXACT-sized to m_meshCount by CreateMeshConstants,
            // not m_indirectCapacity-padded — so an append at slot m_meshCount overruns them unless
            // they are regrown here even when newCap == m_indirectCapacity.
            if (m_meshConstants && static_cast<uint32_t>(m_meshConstants->Size() / sizeof(Mesh_Constants)) < newCap)
            {
                const size_t newMcSize = newCap * sizeof(Mesh_Constants);
                const bool useMcMirror = RHII.GetApi() == PE_GRAPHICS_API_DX12;
                Buffer *newMc = Buffer::Create({
                    .size = newMcSize,
                    .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST |
                             (useMcMirror ? PE_BUFFER_USAGE_TRANSFER_SRC : PE_BUFFER_USAGE_NONE),
                    .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT_DEVICE,
                    .name = "Scene_meshConstants",
                });
                if (m_meshConstants && m_meshCount > 0)
                {
                    CommandBuffer *c = q->AcquireCommandBuffer();
                    c->Begin();
                    c->CopyBuffer(m_meshConstants, newMc, m_meshCount * sizeof(Mesh_Constants), 0, 0);
                    c->End();
                    q->Submit(1, &c, nullptr, nullptr);
                    c->Wait();
                    c->Return();
                }
                RHII.AddToDeletionQueue(
                    [b = m_meshConstants]()
                    { Buffer *buf = b; Buffer::Destroy(buf); });
                m_meshConstants = newMc;

                if (useMcMirror)
                {
                    Buffer *newMcDev = Buffer::Create({
                        .size = newMcSize,
                        .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                        .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY,
                        .name = "Scene_meshConstantsDevice",
                    });
                    if (m_meshConstantsDevice && m_meshCount > 0)
                    {
                        CommandBuffer *c = q->AcquireCommandBuffer();
                        c->Begin();
                        c->CopyBuffer(m_meshConstantsDevice, newMcDev,
                                      m_meshCount * sizeof(Mesh_Constants), 0, 0);
                        c->End();
                        q->Submit(1, &c, nullptr, nullptr);
                        c->Wait();
                        c->Return();
                    }
                    RHII.AddToDeletionQueue([b = m_meshConstantsDevice]()
                                            { Buffer *buf = b; Buffer::Destroy(buf); });
                    m_meshConstantsDevice = newMcDev;
                }
            }
        }

        // The arena buffer already contains all scene geometry (copied from the pre-Reserve
        // buffer).  Clear the dirty flag so that the next FlushPendingGpuWork does NOT call
        // UploadBuffers / CreateGeometryBuffer, which would replace m_buffer with a smaller
        // buffer sized only for the CPU-side mesh list, invalidating the arena extension.
        m_geometryDirty = false;

        // Bump the geometry version so every geoVersion-gated consumer (notably the GBuffer texture
        // descriptor set 1, which binds MeshConstants/materialTable/materialByteBuffer/imageViews)
        // re-points to the buffers this reservation just RECREATED. Without this the GBuffer VS/PS
        // keep reading the freed old m_meshConstants/m_materialTable.
        m_geometryVersion++;

        // This reservation REALLOCATED m_buffer. The BLASes hold raw device addresses into m_buffer
        // (SceneRayTracing.cpp), so after the realloc they point at the freed old buffer: latent UB
        // where RT traversal reads stale memory and can page-fault once that buffer is fence-freed.
        // Mark BLAS dirty so the RT-flush rebuilds them against the relaid-out buffer before the next
        // RT dispatch. Safe under the arena invariant above: that rebuild only reads m_meshes geometry,
        // it never recreates m_buffer. (m_blasDirty already drives a full BLAS+TLAS rebuild.)
        if (RHII.GetCaps().rayTracing)
            m_blasDirty = true;

        return 0;
    }

    bool Scene::GrowArenaVoxelCapacity(CommandBuffer *cmd, uint32_t newVtxCapVertices, size_t newIdxCapBytes)
    {
        if (!cmd || !m_voxelVertexBuf || !m_voxelIndexBuf)
            return false;

        const size_t vtxStride = sizeof(VoxelVertex);
        const bool growVtx = newVtxCapVertices > m_arenaVertexCapacity;
        const bool growIdx = newIdxCapBytes > m_arenaIdxCapacity;
        if (!growVtx && !growIdx)
            return false;

        // Grow without a GPU drain: the live-geometry copy is recorded into the caller's frame voxel cmd
        // (submitted on the main queue before the cull dispatch, like the streamed AddArenaMesh copies).
        // In-flight frames keep reading the OLD buffers — those stay alive and are freed fence-deferred via
        // the deletion queue. The transfer->transfer barrier orders this copy before the same frame's
        // section uploads, which may target reused holes inside the copied region; FlushArenaBarriers then
        // makes the whole buffer visible to the vertex-input stage.
        auto orderCopyBeforeUploads = [&](Buffer *nb, size_t bytes)
        {
            BufferBarrierInfo b{};
            b.buffer = nb;
            b.stageMask = PE_STAGE_TRANSFER;
            b.accessMask = PE_ACCESS_TRANSFER_WRITE;
            b.offset = 0;
            b.size = bytes;
            cmd->BufferBarrier(b);
        };

        if (growVtx)
        {
            const size_t oldBytes = static_cast<size_t>(m_arenaVertexCapacity) * vtxStride;
            Buffer *nb = Buffer::Create({
                .size = static_cast<size_t>(newVtxCapVertices) * vtxStride,
                .usage = PE_BUFFER_USAGE_VERTEX_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_TRANSFER_SRC,
                .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                .name = "voxel_vertex_buffer",
            });
            if (oldBytes > 0)
            {
                cmd->CopyBuffer(m_voxelVertexBuf, nb, oldBytes, 0, 0);
                orderCopyBeforeUploads(nb, oldBytes);
            }
            RHII.AddToDeletionQueue([b = m_voxelVertexBuf]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
            m_voxelVertexBuf = nb;
            m_arenaVertexCapacity = newVtxCapVertices;
        }
        if (growIdx)
        {
            const size_t oldBytes = m_arenaIdxCapacity;
            Buffer *nb = Buffer::Create({
                .size = newIdxCapBytes,
                .usage = PE_BUFFER_USAGE_INDEX_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_TRANSFER_SRC,
                .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                .name = "voxel_index_buffer",
            });
            if (oldBytes > 0)
            {
                cmd->CopyBuffer(m_voxelIndexBuf, nb, oldBytes, 0, 0);
                orderCopyBeforeUploads(nb, oldBytes);
            }
            RHII.AddToDeletionQueue([b = m_voxelIndexBuf]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
            m_voxelIndexBuf = nb;
            m_arenaIdxCapacity = newIdxCapBytes;
        }

        return true;
    }

    int Scene::AddArenaMesh(uint32_t vertexIndex, size_t idxByteOffset,
                            const std::vector<VoxelVertex> &verts,
                            const std::vector<uint16_t> &indices, const AABB &localBox,
                            uint32_t reuseDataOffset, const MeshRuntime &runtimeForImages,
                            bool transparent, CommandBuffer *externalCmd)
    {
        if (!m_buffer || !m_indirectAll || !m_meshConstants || !m_visibility)
            return -1;
        if (!m_voxelVertexBuf || !m_voxelIndexBuf)
            return -1;

        const uint32_t vertCount = static_cast<uint32_t>(verts.size());

        const size_t vtxBytes = verts.size() * sizeof(VoxelVertex);
        const size_t idxBytes = indices.size() * sizeof(uint16_t);

        // Placement is supplied by the GeometryArena's free lists (NOT a bump pointer — ranges are
        // reused after Release). Validate it lands inside the reserved arena region.
        if (vertexIndex < m_arenaVertexBase ||
            static_cast<size_t>(vertexIndex - m_arenaVertexBase) + vertCount > m_arenaVertexCapacity)
        {
            PE_WARN("Scene::AddArenaMesh: vertex placement out of arena range (vtxIdx=%u count=%u base=%u cap=%u)",
                    vertexIndex, vertCount, m_arenaVertexBase, m_arenaVertexCapacity);
            return -1;
        }
        if (idxByteOffset < m_arenaIdxByteBase ||
            (idxByteOffset - m_arenaIdxByteBase) + idxBytes > m_arenaIdxCapacity)
        {
            PE_WARN("Scene::AddArenaMesh: index placement out of arena range (off=%zu bytes=%zu base=%zu cap=%zu)",
                    idxByteOffset, idxBytes, m_arenaIdxByteBase, m_arenaIdxCapacity);
            return -1;
        }
        // Reuse a tombstoned slot (returned to the pool only after the retire delay, so no in-flight
        // frame still reads its Mesh_Constants) before growing into a fresh slot. Fresh slots are bounded
        // by the indirect capacity; reused slots are already within it.
        const bool reuseSlot = !m_arenaFreeSlots.empty();
        if (!reuseSlot && m_meshCount >= m_indirectCapacity)
        {
            PE_WARN("Scene::AddArenaMesh: indirect capacity exceeded (meshCount=%u cap=%u)",
                    m_meshCount, m_indirectCapacity);
            return -1;
        }

        int idx;
        if (reuseSlot)
        {
            // Reuse the LOWEST free slot so live slots stay packed at the bottom and freed slots collect
            // at the top, where FreeArenaSlot trims them off m_meshCount — keeping GetMeshCount() (which
            // sizes the per-frame cull + indirect draw recording, a DX12 hot path) at the live working set
            // instead of the all-time streaming peak.
            size_t minI = 0;
            for (size_t i = 1; i < m_arenaFreeSlots.size(); ++i)
                if (m_arenaFreeSlots[i] < m_arenaFreeSlots[minI])
                    minI = i;
            idx = static_cast<int>(m_arenaFreeSlots[minI]);
            m_arenaFreeSlots[minI] = m_arenaFreeSlots.back();
            m_arenaFreeSlots.pop_back();
        }
        else
        {
            idx = static_cast<int>(m_meshCount);
        }
        Queue *q = RHII.GetMainQueue();

        // Base-0 byte offsets into the dedicated voxel buffers. The shared vertex index keeps the GBuffer
        // Vertex stream and the shadow PositionUvVertex stream aligned: arena vertex k lives at index k in
        // BOTH voxel buffers, so one vertexOffset is valid for the voxel GBuffer draw and the voxel
        // shadow draw alike.
        const size_t vtxByteOff = static_cast<size_t>(vertexIndex) * sizeof(VoxelVertex);

        // Voxel index buffer is bound at byte 0 as UINT16 -> firstIndex = byteOff/2. vertexOffset = the
        // packed voxel vertex index (the same vertexOffset drives the GBuffer and shadow voxel draws).
        const uint32_t firstIndex = static_cast<uint32_t>(idxByteOffset / sizeof(uint16_t));
        const int32_t vertexOffset = static_cast<int32_t>(vertexIndex);

        PeDrawIndexedIndirectCommand drawCmd{};
        drawCmd.indexCount = static_cast<uint32_t>(indices.size());
        drawCmd.instanceCount = 1;
        drawCmd.firstIndex = firstIndex;
        drawCmd.vertexOffset = vertexOffset;
        drawCmd.firstInstance = static_cast<uint32_t>(idx);

        // Record the geometry/indirect/visibility GPU work. When externalCmd is supplied these copies
        // ride the caller's frame upload buffer (submitted on the main queue BEFORE the cull dispatch),
        // and per-section barriers are skipped in favour of one coarse whole-buffer barrier per frame
        // (FlushArenaBarriers): the reads all happen later in the render passes, so a few whole-buffer
        // barriers are equivalent to N per-copy ones and avoid the per-section barrier churn. The
        // standalone path Submit+Waits each section, so it barriers inline.
        const bool inlineBarriers = (externalCmd == nullptr);
        auto recordGeometry = [&](CommandBuffer *cmd)
        {
            cmd->CopyBufferStaged(m_voxelVertexBuf, const_cast<VoxelVertex *>(verts.data()), vtxBytes, vtxByteOff);
            cmd->CopyBufferStaged(m_voxelIndexBuf, const_cast<uint16_t *>(indices.data()), idxBytes, idxByteOffset);
            cmd->CopyBufferStaged(m_indirectAll, &drawCmd, PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
                                  static_cast<size_t>(idx) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
            cmd->FillBuffer(m_visibility, static_cast<size_t>(idx) * sizeof(uint32_t), sizeof(uint32_t), 1u);
            if (inlineBarriers)
            {
                BufferBarrierInfo b{};
                b.buffer = m_voxelVertexBuf;
                b.stageMask = PE_STAGE_VERTEX_INPUT;
                b.accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ;
                b.offset = vtxByteOff;
                b.size = vtxBytes;
                cmd->BufferBarrier(b);
                b.buffer = m_voxelIndexBuf;
                b.accessMask = PE_ACCESS_INDEX_READ;
                b.offset = idxByteOffset;
                b.size = idxBytes;
                cmd->BufferBarrier(b);
                b.buffer = m_indirectAll;
                b.stageMask = PE_STAGE_DRAW_INDIRECT | PE_STAGE_COMPUTE_SHADER;
                b.accessMask = PE_ACCESS_INDIRECT_COMMAND_READ | PE_ACCESS_SHADER_READ;
                b.offset = static_cast<size_t>(idx) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
                b.size = PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
                cmd->BufferBarrier(b);
                b.buffer = m_visibility;
                b.stageMask = PE_STAGE_COMPUTE_SHADER;
                b.accessMask = PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE;
                b.offset = static_cast<size_t>(idx) * sizeof(uint32_t);
                b.size = sizeof(uint32_t);
                cmd->BufferBarrier(b);
            }
        };

        if (externalCmd)
        {
            recordGeometry(externalCmd);
        }
        else
        {
            CommandBuffer *cmd = q->AcquireCommandBuffer();
            cmd->Begin();
            recordGeometry(cmd);
            cmd->End();
            q->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            cmd->Return();
        }

        // Write mesh constants (CPU-mapped buffer; immediate host write).
        const size_t mcOffset = static_cast<size_t>(idx) * sizeof(Mesh_Constants);
        {
            Mesh_Constants mc{};
            mc.alphaCut = 0.0f;
            mc.baseColorAlpha = 1.0f;
            mc.meshDataOffset = reuseDataOffset;
            mc.textureMask = 0u;
            mc.materialId = runtimeForImages.materialGpuIndex;
            for (int k = 0; k < 5; ++k)
                mc.meshImageIndex[k] = runtimeForImages.imageViewIndices[k];
            mc.materialByteOffset = 0xFFFFFFFF;
            // bit2 (0x4) = voxel (cull routes to the voxel bucket / skips standard buckets); bit3 (0x8) =
            // transparent voxel (cull keeps it OUT of the opaque voxel bucket — drawn by the transparent pass).
            mc.editorFlags = transparent ? 0xCu : 0x4u;
            mc.renderType = static_cast<uint32_t>(RenderType::Opaque);
            mc.aabbMinX = localBox.min.x;
            mc.aabbMinY = localBox.min.y;
            mc.aabbMinZ = localBox.min.z;
            mc.aabbMaxX = localBox.max.x;
            mc.aabbMaxY = localBox.max.y;
            mc.aabbMaxZ = localBox.max.z;

            m_meshConstants->Map();
            BufferRange range{};
            range.data = &mc;
            range.offset = mcOffset;
            range.size = sizeof(Mesh_Constants);
            m_meshConstants->Copy(1, &range, true);
            m_meshConstants->Flush(sizeof(Mesh_Constants), mcOffset);
            m_meshConstants->Unmap();
        }

        // DX12: publish the new entry into the GPU-cached DEFAULT mirror (the shaders read the mirror,
        // not the staging buffer). Recorded into externalCmd so it is ordered on the queue before the
        // frame's cull dispatch; otherwise one-shot.
        if (m_meshConstantsDevice)
        {
            auto recordMirror = [&](CommandBuffer *cmd)
            {
                cmd->CopyBuffer(m_meshConstants, m_meshConstantsDevice, sizeof(Mesh_Constants),
                                mcOffset, mcOffset);
                if (inlineBarriers)
                {
                    BufferBarrierInfo barrier{};
                    barrier.buffer = m_meshConstantsDevice;
                    barrier.stageMask = PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_SHADER;
                    barrier.accessMask = PE_ACCESS_SHADER_STORAGE_READ;
                    barrier.offset = mcOffset;
                    barrier.size = sizeof(Mesh_Constants);
                    cmd->BufferBarrier(barrier);
                    m_meshConstantsDevice->GetTrackInfo() = barrier;
                }
            };

            if (externalCmd)
            {
                recordMirror(externalCmd);
            }
            else
            {
                CommandBuffer *cmd2 = q->AcquireCommandBuffer();
                cmd2->Begin();
                recordMirror(cmd2);
                cmd2->End();
                q->Submit(1, &cmd2, nullptr, nullptr);
                cmd2->Wait();
                cmd2->Return();
            }
        }

        // CPU shadow (parallel to slots [m_arenaSlotBase, m_meshCount)). A reused slot writes in place;
        // a fresh slot appends (relIdx == m_arenaSlots.size()) and bumps the high-water m_meshCount.
        ArenaSlot slot{};
        slot.indexCount = drawCmd.indexCount;
        slot.firstIndex = firstIndex;
        slot.vertexOffset = vertexOffset;
        slot.idxByteOffset = idxByteOffset;
        slot.idxBytes = idxBytes;
        slot.vertexCount = vertCount;
        slot.transparent = transparent;
        if (reuseSlot)
        {
            m_arenaSlots[static_cast<uint32_t>(idx) - m_arenaSlotBase] = slot;
        }
        else
        {
            m_arenaSlots.push_back(slot);
            ++m_meshCount;
        }

        m_arenaVertexUsed += vertCount;
        m_arenaIdxUsed += idxBytes;
        // New mesh-constants entry published — re-point geoVersion-gated descriptor sets.
        m_geometryVersion++;
        return idx;
    }

    int Scene::RemoveArenaMesh(int idx, CommandBuffer *externalCmd)
    {
        if (!m_indirectAll || !m_visibility || !m_meshConstants)
            return -1;
        if (idx < static_cast<int>(m_arenaSlotBase) || static_cast<uint32_t>(idx) >= m_meshCount)
        {
            PE_WARN("[Arena] RemoveArenaMesh: idx=%d out of arena range [%u, %u)", idx, m_arenaSlotBase, m_meshCount);
            return -1;
        }

        const uint32_t relIdx = static_cast<uint32_t>(idx) - m_arenaSlotBase;
        const ArenaSlot removed = m_arenaSlots[relIdx]; // copy: cleared below

        // Streaming (externalCmd) batches barriers per frame via FlushArenaBarriers; the standalone path
        // Submit+Waits and barriers inline. See AddArenaMesh for the coarse-barrier rationale.
        const bool inlineBarriers = (externalCmd == nullptr);
        Queue *q = RHII.GetMainQueue();
        auto record = [&](CommandBuffer *cmd)
        {
            // Tombstone the slot in place: zero its indirect draw + visibility so the cull and GBuffer
            // emit nothing, and neuter its index bytes so any stale two-phase-occlusion filtered draw
            // (the filtered buffer is NOT cleared per-frame) degenerates to a no-op. We deliberately do
            // NOT swap-remove/relocate: relocation CPU-rewrites the slot's Mesh_Constants — including the
            // AABB origin the voxel VS adds to every vertex for world position — while in-flight frames
            // still read it (Vulkan reads the host-mapped buffer directly; there is no device mirror),
            // which flung garbage triangles at the streaming front. The slot is reused later via the free
            // pool (FreeArenaSlot, after the GeometryArena retire delay), so its constants are overwritten
            // only once no in-flight frame can still reference it.
            PeDrawIndexedIndirectCommand deadDraw{}; // all-zero: indexCount/instanceCount = 0
            cmd->CopyBufferStaged(m_indirectAll, &deadDraw, PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
                                  static_cast<size_t>(idx) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
            cmd->FillBuffer(m_visibility, static_cast<size_t>(idx) * sizeof(uint32_t), sizeof(uint32_t), 0u);
            if (removed.idxBytes > 0)
                cmd->FillBuffer(m_voxelIndexBuf, removed.idxByteOffset, removed.idxBytes, 0u);

            if (inlineBarriers)
            {
                BufferBarrierInfo b{};
                b.buffer = m_indirectAll;
                b.stageMask = PE_STAGE_DRAW_INDIRECT | PE_STAGE_COMPUTE_SHADER;
                b.accessMask = PE_ACCESS_INDIRECT_COMMAND_READ | PE_ACCESS_SHADER_READ;
                b.offset = static_cast<size_t>(idx) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
                b.size = PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
                cmd->BufferBarrier(b);
                b.buffer = m_visibility;
                b.stageMask = PE_STAGE_COMPUTE_SHADER;
                b.accessMask = PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE;
                b.offset = static_cast<size_t>(idx) * sizeof(uint32_t);
                b.size = sizeof(uint32_t);
                cmd->BufferBarrier(b);
                if (removed.idxBytes > 0)
                {
                    b.buffer = m_voxelIndexBuf;
                    b.stageMask = PE_STAGE_VERTEX_INPUT;
                    b.accessMask = PE_ACCESS_INDEX_READ;
                    b.offset = removed.idxByteOffset;
                    b.size = removed.idxBytes;
                    cmd->BufferBarrier(b);
                }
            }
        };

        if (externalCmd)
        {
            record(externalCmd);
        }
        else
        {
            CommandBuffer *cmd = q->AcquireCommandBuffer();
            cmd->Begin();
            record(cmd);
            cmd->End();
            q->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            cmd->Return();
        }

        // CPU shadow: mark the slot dead in place. m_meshCount stays as the high-water dispatch count
        // (dead slots dispatch a degenerate draw); the slot returns to the reuse pool via FreeArenaSlot.
        m_arenaSlots[relIdx] = ArenaSlot{};
        m_arenaVertexUsed -= removed.vertexCount;
        m_arenaIdxUsed -= removed.idxBytes;
        m_geometryVersion++;
        return -1;
    }

    void Scene::FreeArenaSlot(int idx)
    {
        if (idx < static_cast<int>(m_arenaSlotBase) || static_cast<uint32_t>(idx) >= m_meshCount)
            return;
        m_arenaFreeSlots.push_back(static_cast<uint32_t>(idx));

        // Trim the trailing run of free slots so GetMeshCount() (the per-frame cull + indirect draw count,
        // which dominates DX12 streaming cost) tracks the live working set, not the all-time peak. Only
        // FREE slots (dead + past the retire delay) are trimmed, so the count never drops below a slot an
        // in-flight frame still references.
        bool trimmed = true;
        while (trimmed && m_meshCount > m_arenaSlotBase)
        {
            trimmed = false;
            const uint32_t top = m_meshCount - 1;
            for (size_t i = 0; i < m_arenaFreeSlots.size(); ++i)
            {
                if (m_arenaFreeSlots[i] == top)
                {
                    m_arenaFreeSlots[i] = m_arenaFreeSlots.back();
                    m_arenaFreeSlots.pop_back();
                    m_arenaSlots.pop_back();
                    --m_meshCount;
                    trimmed = true;
                    break;
                }
            }
        }
    }

    std::vector<Scene::VoxelTransparentDraw> Scene::GetVoxelTransparentDraws() const
    {
        // Transparent (water) arena slots are tombstoned to indexCount=0 on release and never relocate
        // while live, so the CPU shadow is the authoritative draw list — no GPU readback. slot index =
        // m_arenaSlotBase + i is the firstInstance the voxel VS reads for its origin/transform.
        std::vector<VoxelTransparentDraw> draws;
        for (size_t i = 0; i < m_arenaSlots.size(); ++i)
        {
            const ArenaSlot &s = m_arenaSlots[i];
            if (!s.transparent || s.indexCount == 0)
                continue;
            draws.push_back({s.indexCount, s.firstIndex, s.vertexOffset,
                             m_arenaSlotBase + static_cast<uint32_t>(i)});
        }
        return draws;
    }

    void Scene::FlushArenaBarriers(CommandBuffer *cmd)
    {
        // One coarse whole-buffer barrier per arena buffer, emitted once per frame after all of the
        // frame's AddArenaMesh/RemoveArenaMesh copies (which skip their own per-section barriers on the
        // streamed path). A few whole-buffer barriers are equivalent for correctness to N per-section
        // ones — the reads all happen later, in the render frame's passes — and avoid the per-section
        // barrier churn on a heavily-streamed frame.
        if (!cmd)
            return;
        auto whole = [&](Buffer *b, uint32_t stage, uint32_t access)
        {
            if (!b)
                return;
            BufferBarrierInfo bi{};
            bi.buffer = b;
            bi.stageMask = stage;
            bi.accessMask = access;
            bi.offset = 0;
            bi.size = b->Size();
            cmd->BufferBarrier(bi);
        };
        whole(m_voxelVertexBuf, PE_STAGE_VERTEX_INPUT, PE_ACCESS_VERTEX_ATTRIBUTE_READ);
        whole(m_voxelIndexBuf, PE_STAGE_VERTEX_INPUT, PE_ACCESS_INDEX_READ);
        whole(m_indirectAll, PE_STAGE_DRAW_INDIRECT | PE_STAGE_COMPUTE_SHADER,
              PE_ACCESS_INDIRECT_COMMAND_READ | PE_ACCESS_SHADER_READ);
        whole(m_visibility, PE_STAGE_COMPUTE_SHADER,
              PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE);
        if (m_meshConstantsDevice)
        {
            whole(m_meshConstantsDevice, PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_SHADER,
                  PE_ACCESS_SHADER_STORAGE_READ);
            BufferBarrierInfo bi{};
            bi.buffer = m_meshConstantsDevice;
            bi.stageMask = PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_SHADER;
            bi.accessMask = PE_ACCESS_SHADER_STORAGE_READ;
            bi.offset = 0;
            bi.size = m_meshConstantsDevice->Size();
            m_meshConstantsDevice->GetTrackInfo() = bi;
        }
    }

    Mesh_Constants Scene::ComputeMeshConstants(uint32_t nodeIndex, int meshIndex) const
    {
        const Mesh &mesh = m_meshes[meshIndex];
        const MeshRuntime &meshRt = m_meshRuntimes[meshIndex];

        float alphaCut = 0.0f;
        float baseColorAlpha = 1.0f;
        uint32_t texMask = 0u;
        if (mesh.materialInstance)
        {
            alphaCut = (mesh.renderType == RenderType::AlphaCut) ? mesh.materialInstance->GetAlphaCutoff() : 0.0f;
            baseColorAlpha = mesh.materialInstance->GetBaseColorFactor().a;
            texMask = mesh.materialInstance->GetTextureMask();
        }
        else if (mesh.material)
        {
            const Material *mat = mesh.material;
            alphaCut = (mesh.renderType == RenderType::AlphaCut) ? mat->alphaCutoff : 0.0f;
            baseColorAlpha = mat->baseColorFactor.a;
            texMask = mat->textureMask;
        }

        Mesh_Constants constants{};
        constants.alphaCut = alphaCut;
        constants.baseColorAlpha = baseColorAlpha;
        constants.meshDataOffset = static_cast<uint32_t>(m_nodeRuntime[nodeIndex].dataOffset);
        constants.textureMask = texMask;
        constants.materialId = meshRt.materialGpuIndex;
        for (int k = 0; k < 5; k++)
            constants.meshImageIndex[k] = meshRt.imageViewIndices[k];
        if (mesh.materialInstance && mesh.materialInstance->gpuByteOffset != 0xFFFFFFFF)
            constants.materialByteOffset = mesh.materialInstance->gpuByteOffset;
        else if (mesh.material && mesh.material->gpuByteOffset != 0xFFFFFFFF)
            constants.materialByteOffset = mesh.material->gpuByteOffset;
        else
            constants.materialByteOffset = 0xFFFFFFFF;

        uint32_t flags = 0;
        if (IsSceneNodeSelected(m_nodeIds[nodeIndex]))
            flags |= 1u;
        if (mesh.material && mesh.material->doubleSided)
            flags |= 2u;
        if (mesh.material && mesh.material->terrain)
            flags |= 16u; // dedicated terrain triplanar pipeline (cull bucket 8)
        constants.editorFlags = flags;
        constants.renderType = static_cast<uint32_t>(mesh.renderType);

        constants.aabbMinX = mesh.boundingBox.min.x;
        constants.aabbMinY = mesh.boundingBox.min.y;
        constants.aabbMinZ = mesh.boundingBox.min.z;
        constants.aabbMaxX = mesh.boundingBox.max.x;
        constants.aabbMaxY = mesh.boundingBox.max.y;
        constants.aabbMaxZ = mesh.boundingBox.max.z;

        constants.lodCount = mesh.lodCount;
        for (uint32_t l = 0; l < Mesh::kMaxLods; ++l)
        {
            constants.lodIndexOffset[l] = mesh.lodIndexOffset[l];
            constants.lodIndexCount[l] = mesh.lodIndexCount[l];
        }
        constants.lodShift = mesh.lodShift;
        constants.lodMeshEnabled = mesh.lodEnabled ? 1u : 0u;
        constants.lodMeshBias = mesh.lodBias;
        return constants;
    }

    bool Scene::TryBindCachedTexture(int meshIndex, int textureSlot, const ResourceHandle<Image> &imageHandle)
    {
        Image *image = imageHandle.get();
        if (!IsValidMeshIndex(meshIndex) || textureSlot < 0 || textureSlot >= 5 || !image)
            return false;

        uint32_t imageViewIndex = 0xFFFFFFFF;
        const auto &defaults = ModelAsset::GetDefaultResources();
        const bool isDefault = image == defaults.black || image == defaults.white || image == defaults.normal;
        if (!isDefault)
        {
            // m_imageViews stores raw views, so keep every descriptor-table image alive with the scene.
            if (std::find_if(m_imageStore.begin(), m_imageStore.end(),
                             [&](const ResourceHandle<Image> &resident)
                             { return resident.get() == image; }) == m_imageStore.end())
                m_imageStore.push_back(imageHandle);

            const auto it = std::find(m_imageViews.begin(), m_imageViews.end(), image->GetSRV());
            if (it == m_imageViews.end())
                return false;
            imageViewIndex = static_cast<uint32_t>(std::distance(m_imageViews.begin(), it));
        }

        MeshRuntime &runtime = m_meshRuntimes[meshIndex];
        if (runtime.imageViewIndices[textureSlot] == imageViewIndex)
            return true;

        runtime.imageViewIndices[textureSlot] = imageViewIndex;
        if (std::find(m_pendingTextureMeshUploads.begin(), m_pendingTextureMeshUploads.end(), meshIndex) ==
            m_pendingTextureMeshUploads.end())
            m_pendingTextureMeshUploads.push_back(meshIndex);
        return true;
    }

    void Scene::RecordPendingTextureUploads(CommandBuffer *cmd)
    {
        if (m_pendingTextureMeshUploads.empty() || !cmd || !m_meshConstants)
            return;

        PE_PROFILE_SCOPE("Texture Mesh Uploads");
        size_t firstOffset = SIZE_MAX;
        size_t lastOffset = 0;
        m_meshConstants->Map();

        for (uint32_t nodeIndex = 0; nodeIndex < GetNodeCount(); ++nodeIndex)
        {
            NodeId *node = m_nodeIds[nodeIndex];
            if (m_nodeRuntime[nodeIndex].gpuPending || !IsNodeHierarchyEnabled(node))
                continue;

            const auto &meshRefs = m_nodeComponentCache[nodeIndex].meshRefs->meshRefs;
            for (uint32_t refSlot = 0; refSlot < meshRefs.size(); ++refSlot)
            {
                const int meshIndex = meshRefs[refSlot];
                if (std::find(m_pendingTextureMeshUploads.begin(), m_pendingTextureMeshUploads.end(), meshIndex) ==
                    m_pendingTextureMeshUploads.end())
                    continue;
                if (!IsValidMeshIndex(meshIndex) || m_meshes[meshIndex].indexCount == 0 ||
                    !IsRasterIndirectMesh(m_meshes[meshIndex]))
                    continue;

                const uint32_t indirectSlot = GetMeshRefIndirectSlot(node, refSlot);
                if (indirectSlot == UINT32_MAX || indirectSlot >= m_meshCount)
                    continue;

                Mesh_Constants constants = ComputeMeshConstants(nodeIndex, meshIndex);
                const size_t offset = static_cast<size_t>(indirectSlot) * sizeof(Mesh_Constants);
                BufferRange range{};
                range.data = &constants;
                range.offset = offset;
                range.size = sizeof(Mesh_Constants);
                m_meshConstants->Copy(1, &range, true);

                if (m_meshConstantsDevice)
                    cmd->CopyBuffer(m_meshConstants, m_meshConstantsDevice, sizeof(Mesh_Constants), offset, offset);
                firstOffset = std::min(firstOffset, offset);
                lastOffset = std::max(lastOffset, offset + sizeof(Mesh_Constants));
            }
        }

        if (firstOffset != SIZE_MAX)
            m_meshConstants->Flush(lastOffset - firstOffset, firstOffset);
        m_meshConstants->Unmap();

        if (m_meshConstantsDevice && firstOffset != SIZE_MAX)
        {
            BufferBarrierInfo barrier{};
            barrier.buffer = m_meshConstantsDevice;
            barrier.stageMask = PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_SHADER;
            barrier.accessMask = PE_ACCESS_SHADER_STORAGE_READ;
            barrier.offset = firstOffset;
            barrier.size = lastOffset - firstOffset;
            cmd->BufferBarrier(barrier);
            m_meshConstantsDevice->GetTrackInfo() = barrier;
        }
        m_pendingTextureMeshUploads.clear();
    }

    uint32_t Scene::GetMeshRefIndirectSlot(const NodeId *node, uint32_t refSlot) const
    {
        if (!node)
            return UINT32_MAX;
        ValidateNodeId(node);
        const auto &slots = m_nodeRuntime[node->index].meshRefIndirect;
        return refSlot < slots.size() ? slots[refSlot] : UINT32_MAX;
    }

    bool Scene::UpdateStreamedMesh(NodeId *node, uint32_t refSlot, int meshIndex,
                                   uint32_t vertexCopyCount, uint32_t indexCopyCount, CommandBuffer *cmd)
    {
        if (!cmd || !m_buffer || !m_indirectAll || !m_meshConstants || !IsValidMeshIndex(meshIndex))
            return false;
        const uint32_t slot = GetMeshRefIndirectSlot(node, refSlot);
        if (slot == UINT32_MAX || slot >= m_meshCount)
            return false;
        const Mesh &mesh = m_meshes[meshIndex];

        // The mesh's reserved ranges must sit inside the GPU buffer laid out at the last rebuild —
        // store data appended after that rebuild has no GPU backing yet and can't be streamed into.
        if (static_cast<size_t>(mesh.vertexOffset) + vertexCopyCount > m_verticesCount ||
            static_cast<size_t>(mesh.positionsOffset) + vertexCopyCount > m_positionsCount ||
            static_cast<size_t>(mesh.indexOffset) + indexCopyCount > m_indicesCount ||
            mesh.aabbVertexOffset + 8 > m_aabbVerticesCount)
        {
            PE_WARN("Scene::UpdateStreamedMesh: mesh %d ranges outside the uploaded geometry buffer", meshIndex);
            return false;
        }

        BufferBarrierInfo b{};
        b.buffer = m_buffer;
        b.stageMask = PE_STAGE_VERTEX_INPUT;
        if (vertexCopyCount > 0)
        {
            const size_t vOff = m_verticesOffset + static_cast<size_t>(mesh.vertexOffset) * sizeof(Vertex);
            cmd->CopyBufferStaged(m_buffer, m_vertexStore.data() + mesh.vertexOffset,
                                  static_cast<size_t>(vertexCopyCount) * sizeof(Vertex), vOff);
            b.accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ;
            b.offset = vOff;
            b.size = static_cast<size_t>(vertexCopyCount) * sizeof(Vertex);
            cmd->BufferBarrier(b);

            // Depth-prepass/shadows bind the PositionUvVertex stream at the same vertex index.
            const size_t pOff =
                m_positionsOffset + static_cast<size_t>(mesh.positionsOffset) * sizeof(PositionUvVertex);
            cmd->CopyBufferStaged(m_buffer, m_positionUvStore.data() + mesh.positionsOffset,
                                  static_cast<size_t>(vertexCopyCount) * sizeof(PositionUvVertex), pOff);
            b.offset = pOff;
            b.size = static_cast<size_t>(vertexCopyCount) * sizeof(PositionUvVertex);
            cmd->BufferBarrier(b);
        }
        if (indexCopyCount > 0)
        {
            const size_t iOff = static_cast<size_t>(mesh.indexOffset) * sizeof(uint32_t);
            cmd->CopyBufferStaged(m_buffer, m_indexStore.data() + mesh.indexOffset,
                                  static_cast<size_t>(indexCopyCount) * sizeof(uint32_t), iOff);
            b.accessMask = PE_ACCESS_INDEX_READ;
            b.offset = iOff;
            b.size = static_cast<size_t>(indexCopyCount) * sizeof(uint32_t);
            cmd->BufferBarrier(b);
        }
        // 8 AABB corner vertices (Hi-Z occluder proxy / debug AABB draw).
        const size_t aOff = m_aabbVerticesOffset + mesh.aabbVertexOffset * sizeof(AabbVertex);
        cmd->CopyBufferStaged(m_buffer, m_aabbVertexStore.data() + mesh.aabbVertexOffset,
                              8 * sizeof(AabbVertex), aOff);
        b.accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ;
        b.offset = aOff;
        b.size = 8 * sizeof(AabbVertex);
        cmd->BufferBarrier(b);

        // Indirect draw template: live indexCount (the budget tail is never drawn). LOD-enabled meshes
        // get firstIndex/indexCount overridden by the cull from Mesh_Constants anyway.
        PeDrawIndexedIndirectCommand drawCmd{};
        drawCmd.indexCount = mesh.indexCount;
        drawCmd.instanceCount = 1;
        drawCmd.firstIndex = mesh.indexOffset;
        drawCmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffset);
        drawCmd.firstInstance = slot;
        cmd->CopyBufferStaged(m_indirectAll, &drawCmd, PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
                              static_cast<size_t>(slot) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
        b.buffer = m_indirectAll;
        b.stageMask = PE_STAGE_DRAW_INDIRECT | PE_STAGE_COMPUTE_SHADER;
        b.accessMask = PE_ACCESS_INDIRECT_COMMAND_READ | PE_ACCESS_SHADER_READ;
        b.offset = static_cast<size_t>(slot) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
        b.size = PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
        cmd->BufferBarrier(b);

        // Mesh_Constants (host-mapped write + DX12 device mirror), the AddArenaMesh pattern.
        Mesh_Constants mc = ComputeMeshConstants(node->index, meshIndex);
        const size_t mcOffset = static_cast<size_t>(slot) * sizeof(Mesh_Constants);
        m_meshConstants->Map();
        BufferRange range{};
        range.data = &mc;
        range.offset = mcOffset;
        range.size = sizeof(Mesh_Constants);
        m_meshConstants->Copy(1, &range, true);
        m_meshConstants->Flush(sizeof(Mesh_Constants), mcOffset);
        m_meshConstants->Unmap();
        if (m_meshConstantsDevice)
        {
            cmd->CopyBuffer(m_meshConstants, m_meshConstantsDevice, sizeof(Mesh_Constants), mcOffset, mcOffset);
            BufferBarrierInfo mb{};
            mb.buffer = m_meshConstantsDevice;
            mb.stageMask = PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_SHADER;
            mb.accessMask = PE_ACCESS_SHADER_STORAGE_READ;
            mb.offset = mcOffset;
            mb.size = sizeof(Mesh_Constants);
            cmd->BufferBarrier(mb);
            m_meshConstantsDevice->GetTrackInfo() = mb;
        }
        return true;
    }

} // namespace pe
