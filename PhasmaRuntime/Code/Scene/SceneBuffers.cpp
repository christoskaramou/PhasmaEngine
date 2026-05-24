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
#include "API/RHI.h"
#include "API/Vertex.h"

namespace pe
{
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

        PeBufferUsageFlags geometryUsage = PE_BUFFER_USAGE_TRANSFER_DST | PE_BUFFER_USAGE_INDEX_BUFFER |
                                           PE_BUFFER_USAGE_VERTEX_BUFFER | PE_BUFFER_USAGE_STORAGE_BUFFER |
                                           PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS;
        if (RHII.GetCaps().rayTracing)
            geometryUsage |= PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR;

        m_buffer = Buffer::Create({
            .size = m_aabbVerticesOffset + sizeof(AabbVertex) * m_aabbVerticesCount,
            .usage = geometryUsage,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = "combined_Geometry_buffer",
        });
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
        auto &gSettings = Settings::Get<GlobalSettings>();
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

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            m_nodeRuntime[i].hasUniformData = false;
            m_nodeRuntime[i].dataOffset = static_cast<size_t>(-1);

            if (m_nodeRuntime[i].gpuPending || !IsNodeHierarchyEnabled(m_nodeIds[i]))
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
            storage = Buffer::Create({
                .size = storageSize,
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER,
                .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT,
                .name = "storage_Geometry_buffer_" + std::to_string(i++),
            });
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
                if (meshIdx < 0)
                    continue;

                const Mesh &mesh = m_meshes[meshIdx];
                if (mesh.indexCount == 0)
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
            .usage = PE_BUFFER_USAGE_INDIRECT_BUFFER | PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
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
                .size = 7 * sizeof(uint32_t),
                .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_INDIRECT_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
                .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
                .name = "culling_counters_" + std::to_string(i),
            });
        }

        m_indirectOpaqueSS = createFilteredIndirect("indirect_OpaqueSS_");
        m_indirectAlphaCutSS = createFilteredIndirect("indirect_AlphaCutSS_");
        m_indirectOpaqueDS = createFilteredIndirect("indirect_OpaqueDS_");
        m_indirectAlphaCutDS = createFilteredIndirect("indirect_AlphaCutDS_");
        m_indirectAlphaBlend = createFilteredIndirect("indirect_AlphaBlend_");
        m_indirectTransmission = createFilteredIndirect("indirect_Transmission_");
        m_indirectSelected = createFilteredIndirect("indirect_Selected_");

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

        for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
        {
            if (!IsNodeHierarchyEnabled(m_nodeIds[ni]))
                continue;

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
        Buffer::Destroy(m_materialTable);

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

        Buffer::Destroy(m_materialByteBuffer);
        m_materialByteBuffer = nullptr;
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
                if (meshIdx >= 0 && m_meshes[meshIdx].materialInstance)
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
                if (meshIdx < 0)
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
                if (meshIdx < 0)
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
        Buffer::Destroy(m_meshConstants);
        const size_t meshConstantsCapacity = std::max<size_t>(1, m_meshCount);
        m_meshConstants = Buffer::Create({
            .size = meshConstantsCapacity * sizeof(Mesh_Constants),
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
            .name = "Scene_meshConstants",
        });

        size_t offset = 0;
        m_hasTransparentMeshes = false;
        m_hasAlphaBlendMeshes = false;
        m_hasTransmissionMeshes = false;
        m_alphaBlendMeshCount = 0;
        m_transmissionMeshCount = 0;
        m_meshConstants->Map();
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending || !IsNodeHierarchyEnabled(m_nodeIds[i]))
                continue;

            for (int meshIdx : m_nodeComponentCache[i].meshRefs->meshRefs)
            {
                if (meshIdx < 0)
                    continue;

                const Mesh &mesh = m_meshes[meshIdx];
                if (mesh.indexCount == 0)
                    continue;

                if (mesh.renderType == RenderType::AlphaBlend)
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
                if (mesh.materialInstance && mesh.materialInstance->gpuByteOffset != 0xFFFFFFFF)
                    constants.materialByteOffset = mesh.materialInstance->gpuByteOffset;
                else if (mesh.material && mesh.material->gpuByteOffset != 0xFFFFFFFF)
                    constants.materialByteOffset = mesh.material->gpuByteOffset;
                else
                    constants.materialByteOffset = 0xFFFFFFFF;

                uint32_t flags = 0;
                if (IsSceneNodeSelected(m_nodeIds[i]))
                    flags |= 1u;
                bool isDoubleSided = mesh.material && mesh.material->doubleSided;
                if (isDoubleSided)
                    flags |= 2u;
                constants.editorFlags = flags;
                constants.renderType = static_cast<uint32_t>(mesh.renderType);

                constants.aabbMinX = mesh.boundingBox.min.x;
                constants.aabbMinY = mesh.boundingBox.min.y;
                constants.aabbMinZ = mesh.boundingBox.min.z;
                constants.aabbMaxX = mesh.boundingBox.max.x;
                constants.aabbMaxY = mesh.boundingBox.max.y;
                constants.aabbMaxZ = mesh.boundingBox.max.z;

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
        destroyBufferVec(m_indirectOpaqueSS);
        destroyBufferVec(m_indirectAlphaCutSS);
        destroyBufferVec(m_indirectOpaqueDS);
        destroyBufferVec(m_indirectAlphaCutDS);
        destroyBufferVec(m_indirectAlphaBlend);
        destroyBufferVec(m_indirectTransmission);
        destroyBufferVec(m_indirectSelected);
        destroyBufferVec(m_sortKeysAlphaBlend);
        destroyBufferVec(m_sortKeysTransmission);

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
        RHII.WaitDeviceIdle();

        for (auto &storage : m_storages)
        {
            if (storage)
            {
                RHII.AddToDeletionQueue([b = storage]()
                                        { Buffer *buf = b; Buffer::Destroy(buf); });
                storage = nullptr;
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
        destroyBufferVecEager(m_indirectOpaqueSS);
        destroyBufferVecEager(m_indirectAlphaCutSS);
        destroyBufferVecEager(m_indirectOpaqueDS);
        destroyBufferVecEager(m_indirectAlphaCutDS);
        destroyBufferVecEager(m_indirectAlphaBlend);
        destroyBufferVecEager(m_indirectTransmission);
        destroyBufferVecEager(m_indirectSelected);
        destroyBufferVecEager(m_sortKeysAlphaBlend);
        destroyBufferVecEager(m_sortKeysTransmission);
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
