#include "Scene/Scene.h"
#include "Scene/Material.h"
#include "Scene/MaterialReflection.h"
#include "Scene/ModelAsset.h"
#include "Scene/PassInfoAsset.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Vertex.h"
#include "RenderPasses/GbufferPass.h"

namespace pe
{
    void Scene::UploadBuffers(CommandBuffer *cmd)
    {
        // Mesh offsets are already set by model import/AddModel — no per-model fixup needed

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
        // RT acceleration structures are rebuilt in FlushPendingGpuWork via m_blasDirty
    }

    void Scene::CreateGeometryBuffer()
    {
        // Count drawable mesh refs across all nodes
        m_meshCount = 0;
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending)
                continue;
            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (meshIdx >= 0 && m_meshes[meshIdx].indexCount > 0)
                    m_meshCount++;
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

        m_buffer = Buffer::Create(
            m_aabbVerticesOffset + sizeof(AabbVertex) * m_aabbVerticesCount,
            vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eIndexBuffer | vk::BufferUsageFlagBits2::eVertexBuffer | vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eShaderDeviceAddress | vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            "combined_Geometry_buffer");
    }

    void Scene::CopyIndices(CommandBuffer *cmd)
    {
        // Single contiguous copy from Scene's index store
        if (m_indicesCount > 0)
        {
            cmd->CopyBufferStaged(m_buffer, m_indexStore.data(), m_indicesCount * sizeof(uint32_t), 0);

            BufferBarrierInfo indexBarrierInfo{};
            indexBarrierInfo.buffer = m_buffer;
            indexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            indexBarrierInfo.accessMask = vk::AccessFlagBits2::eIndexRead;
            indexBarrierInfo.size = m_indicesCount * sizeof(uint32_t);
            indexBarrierInfo.offset = 0;
            cmd->BufferBarrier(indexBarrierInfo);
        }

        cmd->CopyBufferStaged(m_buffer, s_aabbIndices.data(), s_aabbIndices.size() * sizeof(uint32_t), m_aabbIndicesOffset);

        if (s_aabbIndices.size() > 0)
        {
            BufferBarrierInfo aabbIndexBarrierInfo{};
            aabbIndexBarrierInfo.buffer = m_buffer;
            aabbIndexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            aabbIndexBarrierInfo.accessMask = vk::AccessFlagBits2::eIndexRead;
            aabbIndexBarrierInfo.size = s_aabbIndices.size() * sizeof(uint32_t);
            aabbIndexBarrierInfo.offset = m_aabbIndicesOffset;
            cmd->BufferBarrier(aabbIndexBarrierInfo);
        }
    }

    void Scene::CopyVertices(CommandBuffer *cmd)
    {
        auto &gSettings = Settings::Get<GlobalSettings>();
        auto &progress = gSettings.loading.current;
        auto &total = gSettings.loading.total;

        total = m_verticesCount + m_positionsCount + m_aabbVerticesCount;
        progress = 0;
        gSettings.loading.SetName("Uploading to GPU");

        // Single contiguous copy for each vertex type
        if (m_verticesCount > 0)
        {
            cmd->CopyBufferStaged(m_buffer, m_vertexStore.data(), m_verticesCount * sizeof(Vertex), m_verticesOffset);
            progress += m_verticesCount;

            BufferBarrierInfo vertexBarrierInfo{};
            vertexBarrierInfo.buffer = m_buffer;
            vertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            vertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
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
            posVertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            posVertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
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
            aabbVertexBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eVertexInput;
            aabbVertexBarrierInfo.accessMask = vk::AccessFlagBits2::eVertexAttributeRead;
            aabbVertexBarrierInfo.size = m_aabbVerticesCount * sizeof(AabbVertex);
            aabbVertexBarrierInfo.offset = m_aabbVerticesOffset;
            cmd->BufferBarrier(aabbVertexBarrierInfo);
        }
    }

    void Scene::CreateStorageBuffers()
    {
        size_t storageSize = sizeof(PerFrameData);
        storageSize += RHII.AlignStorageAs(m_meshCount * sizeof(uint32_t), 64);

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            m_nodeRuntime[i].hasUniformData = false; // reset; set true below if drawable

            if (m_nodeRuntime[i].gpuPending)
                continue;

            bool hasDrawable = false;
            for (int mr : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (mr >= 0 && m_meshes[mr].indexCount > 0)
                {
                    hasDrawable = true;
                    break;
                }
            }
            if (!hasDrawable)
                continue;

            m_nodeRuntime[i].hasUniformData = true;
            m_nodeRuntime[i].dataOffset = storageSize;
            int jointCount = GetSkeleton().GetBoneCount();
            size_t nodeDataSize = sizeof(NodeGpuData) + jointCount * sizeof(mat4);
            storageSize += nodeDataSize;
        }

        uint32_t i = 0;
        for (auto &storage : m_storages)
        {
            storage = Buffer::Create(
                storageSize,
                vk::BufferUsageFlagBits2::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                "storage_Geometry_buffer_" + std::to_string(i++));
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
        m_indirectCommands.clear();
        m_indirectCommands.reserve(m_meshCount);

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending)
                continue;

            const auto &refs = m_nodeComponentCache[i].meshRefs->meshRefs;
            m_nodeRuntime[i].meshRefIndirect.resize(refs.size());

            for (uint32_t slot = 0; slot < static_cast<uint32_t>(refs.size()); slot++)
            {
                int meshIdx = refs[slot];
                if (meshIdx < 0)
                    continue;

                const Mesh &mesh = m_meshes[meshIdx];
                if (mesh.indexCount == 0)
                    continue;

                m_nodeRuntime[i].meshRefIndirect[slot] = indirectCount;

                vk::DrawIndexedIndirectCommand indirectCommand{};
                indirectCommand.indexCount = mesh.indexCount;
                indirectCommand.instanceCount = 1;
                indirectCommand.firstIndex = mesh.indexOffset;
                indirectCommand.vertexOffset = mesh.vertexOffset;
                indirectCommand.firstInstance = indirectCount;
                m_indirectCommands.push_back(indirectCommand);

                indirectCount++;
            }
        }

        PE_ERROR_IF(indirectCount != m_meshCount, "Scene::UploadBuffers: Indirect count mismatch!");

        m_indirectAll = Buffer::Create(
            indirectCount * sizeof(vk::DrawIndexedIndirectCommand),
            vk::BufferUsageFlagBits2::eIndirectBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            "indirect_Geometry_buffer_all");
        cmd->CopyBufferStaged(m_indirectAll, m_indirectCommands.data(), m_indirectCommands.size() * sizeof(vk::DrawIndexedIndirectCommand), 0);

        if (indirectCount > 0)
        {
            BufferBarrierInfo indirectBarrierInfo{};
            indirectBarrierInfo.buffer = m_indirectAll;
            indirectBarrierInfo.stageMask = vk::PipelineStageFlagBits2::eDrawIndirect;
            indirectBarrierInfo.accessMask = vk::AccessFlagBits2::eIndirectCommandRead;
            indirectBarrierInfo.size = indirectCount * sizeof(vk::DrawIndexedIndirectCommand);
            indirectBarrierInfo.offset = 0;
            cmd->BufferBarrier(indirectBarrierInfo);
        }

        uint32_t i = 0;
        for (auto &indirect : m_indirects)
        {
            indirect = Buffer::Create(
                indirectCount * sizeof(vk::DrawIndexedIndirectCommand),
                vk::BufferUsageFlagBits2::eIndirectBuffer | vk::BufferUsageFlagBits2::eTransferDst,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                "indirect_Geometry_buffer_" + std::to_string(i++));
        }
    }

    void Scene::UpdateImageViews()
    {
        m_imageViews.clear();
        m_imageViews.reserve(m_imageStore.size());

        const auto &defaults = ModelAsset::GetDefaultResources();
        OrderedMap<Image *, uint32_t> imagesMap{};

        // Build unique image view list from Scene's image store
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

        // Map each active mesh's texture slots to image view indices.
        // Only iterate meshes referenced by live nodes to avoid stale entries
        // left behind by deleted models (m_meshes is append-only).
        for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
        {
            for (int meshIndex : m_nodeComponentCache[ni].meshRefs->meshRefs)
            {
                if (meshIndex < 0 || meshIndex >= static_cast<int>(m_meshes.size()))
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
                        // Ensure instance texture overrides are in the image view list
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
            }
        }

        m_geometryVersion++;
    }

    void Scene::CreateMaterialTable()
    {
        Buffer::Destroy(m_materialTable);

        // Assign GPU indices to materials and build the table data.
        // Materials with an existing Material* share indices; legacy meshes get individual entries.
        std::vector<MaterialGpuData> tableData;

        // Reset GPU indices on all materials (owned by Scene and by each ModelAsset)
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
            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (meshIdx < 0)
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
            // Always have at least one entry (default material) to avoid zero-size buffer
            MaterialGpuData defaultMat{};
            defaultMat.baseColorFactor = vec4(1.f);
            defaultMat.emissiveTransmission = vec4(0.f);
            defaultMat.pbrParams = vec4(0.f, 1.f, 0.5f, 1.f);
            tableData.push_back(defaultMat);
        }

        m_materialTable = Buffer::Create(
            tableData.size() * sizeof(MaterialGpuData),
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "Scene_materialTable");

        m_materialTable->Map();
        BufferRange range{};
        range.data = tableData.data();
        range.offset = 0;
        range.size = tableData.size() * sizeof(MaterialGpuData);
        m_materialTable->Copy(1, &range, true);
        m_materialTable->Flush();
        m_materialTable->Unmap();

        // --- ByteAddressBuffer for shader-driven materials ---
        Buffer::Destroy(m_materialByteBuffer);
        m_materialByteBuffer = nullptr;
        m_materialByteBufferUsed = 0;

        // First pass: reflect layouts and compute total byte size needed
        struct ByteMaterialEntry
        {
            Material *mat;
            std::vector<uint8_t> data;
        };
        std::vector<ByteMaterialEntry> byteEntries;
        uint32_t totalBytes = 0;

        // Cache reflected layouts per PassInfoAsset to avoid redundant reflection
        std::unordered_map<std::string, MaterialLayout> layoutCache;

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (meshIdx < 0)
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

                if (!layout.valid || layout.structMembers.empty())
                    continue;

                // Skip if already assigned (shared materials)
                if (mat->gpuByteOffset != 0xFFFFFFFF)
                    continue;

                std::vector<uint8_t> byteData;
                if (mesh.materialInstance)
                    byteData = mesh.materialInstance->BuildByteAddressData(layout.structMembers);
                else
                    byteData = mat->BuildByteAddressData(layout.structMembers);

                if (byteData.empty())
                    continue;

                mat->gpuByteSize = static_cast<uint32_t>(byteData.size());
                mat->gpuByteOffset = totalBytes;
                totalBytes += (static_cast<uint32_t>(byteData.size()) + 3u) & ~3u;
                byteEntries.push_back({mat, std::move(byteData)});
            }
        }

        if (totalBytes == 0)
            totalBytes = 4;

        m_materialByteBuffer = Buffer::Create(
            totalBytes,
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "Scene_materialByteBuffer");

        if (!byteEntries.empty())
        {
            m_materialByteBuffer->Map();
            for (const auto &entry : byteEntries)
            {
                BufferRange br{};
                br.data = const_cast<uint8_t *>(entry.data.data());
                br.offset = entry.mat->gpuByteOffset;
                br.size = entry.data.size();
                m_materialByteBuffer->Copy(1, &br, true);
            }
            m_materialByteBuffer->Flush();
            m_materialByteBuffer->Unmap();
        }
        m_materialByteBufferUsed = totalBytes;
    }

    void Scene::UpdateDirtyMaterials()
    {
        if (!m_materialTable)
            return;

        bool anyDirty = false;

        // Check all owned materials (Scene + ModelAssets)
        auto updateIfDirty = [&](Material *mat)
        {
            if (!mat->dirty || mat->gpuIndex == 0xFFFFFFFF)
                return;

            MaterialGpuData data = mat->BuildGpuData();

            m_materialTable->Map();
            BufferRange range{};
            range.data = &data;
            range.offset = mat->gpuIndex * sizeof(MaterialGpuData);
            range.size = sizeof(MaterialGpuData);
            m_materialTable->Copy(1, &range, true);
            m_materialTable->Flush();
            m_materialTable->Unmap();

            mat->dirty = false;
            anyDirty = true;
        };

        for (auto &mat : m_ownedMaterials)
            updateIfDirty(mat.get());
        for (auto *model : m_models)
            for (auto &mat : model->GetOwnedMaterials())
                updateIfDirty(mat.get());

        if (anyDirty)
            m_geometryVersion++;
    }

    void Scene::CreateMeshConstants(CommandBuffer *cmd)
    {
        Buffer::Destroy(m_meshConstants);
        m_meshConstants = Buffer::Create(
            m_meshCount * sizeof(Mesh_Constants),
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            "Scene_meshConstants");

        size_t offset = 0;
        m_meshConstants->Map();
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending)
                continue;

            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (meshIdx < 0)
                    continue;

                const Mesh &mesh = m_meshes[meshIdx];
                if (mesh.indexCount == 0)
                    continue;

                const MeshRuntime &meshRt = m_meshRuntimes[meshIdx];

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
                constants.meshDataOffset = static_cast<uint32_t>(m_nodeRuntime[i].dataOffset);
                constants.textureMask = texMask;
                constants.materialId = meshRt.materialGpuIndex;
                for (int k = 0; k < 5; k++)
                    constants.meshImageIndex[k] = meshRt.imageViewIndices[k];
                constants.materialByteOffset = (mesh.material && mesh.material->gpuByteOffset != 0xFFFFFFFF)
                                                   ? mesh.material->gpuByteOffset
                                                   : 0xFFFFFFFF;
                constants.pad0 = 0;

                BufferRange range{};
                range.data = &constants;
                range.offset = offset;
                range.size = sizeof(Mesh_Constants);
                m_meshConstants->Copy(1, &range, true);

                offset += sizeof(Mesh_Constants);
            }
        }
        m_meshConstants->Flush();
        m_meshConstants->Unmap();
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

        for (auto &indirect : m_indirects)
        {
            if (indirect)
            {
                RHII.AddToDeletionQueue([b = indirect]()
                                        { Buffer* buf = b; Buffer::Destroy(buf); });
                indirect = nullptr;
            }
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
    }
    void Scene::RebuildRasterInstances(CommandBuffer *cmd)
    {
        // Drain all in-flight frames before destroying buffers that may still be bound
        RHII.WaitDeviceIdle();

        // Release old per-frame storage/indirect buffers before recreating them
        for (auto &storage : m_storages)
        {
            if (storage)
            {
                RHII.AddToDeletionQueue([b = storage]()
                                        { Buffer *buf = b; Buffer::Destroy(buf); });
                storage = nullptr;
            }
        }
        for (auto &indirect : m_indirects)
        {
            if (indirect)
            {
                RHII.AddToDeletionQueue([b = indirect]()
                                        { Buffer *buf = b; Buffer::Destroy(buf); });
                indirect = nullptr;
            }
        }
        if (m_indirectAll)
        {
            RHII.AddToDeletionQueue([b = m_indirectAll]()
                                    { Buffer *buf = b; Buffer::Destroy(buf); });
            m_indirectAll = nullptr;
        }

        // Recalculate m_meshCount from current node mesh refs (no geometry buffer rebuild)
        m_meshCount = 0;
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending)
                continue;
            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (meshIdx >= 0 && m_meshes[meshIdx].indexCount > 0)
                    m_meshCount++;
            }
        }

        CreateStorageBuffers();
        MarkUniformsDirty();
        CreateIndirectBuffers(cmd);
        UpdateImageViews();
        CreateMaterialTable();
        CreateMeshConstants(cmd);
    }
} // namespace pe
