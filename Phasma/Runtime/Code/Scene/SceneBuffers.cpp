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
        return mesh.renderType != RenderType::Lines;
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
                                           PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS;
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
                .size = 7 * sizeof(uint32_t),
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
            vb.accessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE;
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

        for (uint32_t ni = 0; ni < GetNodeCount(); ni++)
        {
            if (!IsNodeHierarchyEnabled(m_nodeIds[ni]))
                continue;

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
        Buffer::Destroy(m_meshConstants);
        Buffer::Destroy(m_meshConstantsDevice);
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

                if (mesh.renderType == RenderType::Lines)
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

                constants.lodCount = mesh.lodCount;
                for (uint32_t l = 0; l < Mesh::kMaxLods; ++l)
                {
                    constants.lodIndexOffset[l] = mesh.lodIndexOffset[l];
                    constants.lodIndexCount[l] = mesh.lodIndexCount[l];
                }
                constants.lodShift = mesh.lodShift;
                constants.lodMeshEnabled = mesh.lodEnabled ? 1u : 0u;
                constants.lodMeshBias = mesh.lodBias;

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
            barrier.accessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ;
            barrier.offset = 0;
            barrier.size = m_meshConstants->Size();
            cmd->BufferBarrier(barrier);
            m_meshConstantsDevice->GetTrackInfo() = barrier;
        }
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

        // CRITICAL LAYOUT INVARIANT (verified by spike): the combined geometry buffer is
        //   [indices][aabbIndices][vertices (Vertex)][positions (PositionUvVertex)][aabbVertices]
        // The GBuffer binds the vertex stream at m_verticesOffset (stride sizeof(Vertex)); the
        // DEPTH PREPASS binds the position stream at m_positionsOffset (stride sizeof(PositionUvVertex)).
        // Both passes index with the SAME per-draw `vertexOffset`. So a draw's Vertex index and its
        // PositionUvVertex index MUST be EQUAL. A naive tail-append (independent vtx/posUv tail bases)
        // breaks this: vertexOffset is right for the GBuffer Vertex stream but points at the wrong
        // PositionUv entry, so the depth prepass writes garbage/no depth and the GBuffer's depth-EQUAL
        // test rejects every fragment -> the mesh is invisible. We therefore RE-LAY-OUT the buffer:
        // extend the vertices region AND the positions region by the same vertex headroom, so arena
        // vertex k lives at index (origVertexCount + k) in BOTH streams.
        const size_t vtxStride = sizeof(Vertex);
        const size_t posUvStride = sizeof(PositionUvVertex);
        const size_t idxStride = sizeof(uint32_t);
        // Headroom expressed in VERTICES (shared by both streams) and INDICES.
        const uint32_t arenaVertCap = static_cast<uint32_t>(
            std::max<size_t>(vtxHeadroomBytes / vtxStride, posUvHeadroomBytes / posUvStride));
        const uint32_t arenaIdxCap = static_cast<uint32_t>((idxHeadroomBytes + idxStride - 1) / idxStride);

        Queue *q = RHII.GetMainQueue();
        q->WaitIdle();

        const uint32_t sharedVertexBase = std::max(m_verticesCount, m_positionsCount);
        const size_t origVerticesBytes = static_cast<size_t>(m_verticesCount) * vtxStride;
        const size_t origPositionsBytes = static_cast<size_t>(m_positionsCount) * posUvStride;
        const size_t sharedVertexSpanBytes = static_cast<size_t>(sharedVertexBase) * vtxStride;
        const size_t sharedPositionSpanBytes = static_cast<size_t>(sharedVertexBase) * posUvStride;
        const size_t origAabbVertBytes = static_cast<size_t>(m_aabbVerticesCount) * sizeof(AabbVertex);

        // New region offsets (indices/aabbIndices unchanged; arena index headroom is a tail).
        const size_t newVerticesOffset = m_verticesOffset; // unchanged
        const size_t vtxRegionBytes = sharedVertexSpanBytes + static_cast<size_t>(arenaVertCap) * vtxStride;
        const size_t newPositionsOffset = newVerticesOffset + vtxRegionBytes;
        const size_t posRegionBytes = sharedPositionSpanBytes + static_cast<size_t>(arenaVertCap) * posUvStride;
        const size_t newAabbVerticesOffset = newPositionsOffset + posRegionBytes;
        const size_t arenaIdxByteBase = newAabbVerticesOffset + origAabbVertBytes; // index headroom tail
        const size_t newSize = arenaIdxByteBase + static_cast<size_t>(arenaIdxCap) * idxStride;

        PeBufferUsageFlags geometryUsage =
            PE_BUFFER_USAGE_TRANSFER_SRC | PE_BUFFER_USAGE_TRANSFER_DST |
            PE_BUFFER_USAGE_INDEX_BUFFER | PE_BUFFER_USAGE_VERTEX_BUFFER |
            PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS;
        if (RHII.GetCaps().rayTracing)
            geometryUsage |=
                PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR;

        Buffer *newBuf = Buffer::Create({
            .size = newSize,
            .usage = geometryUsage,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = "arena_geometry_buffer",
        });

        // Copy each original sub-region to its NEW location (indices+aabbIndices stay at [0..m_verticesOffset)).
        {
            CommandBuffer *cmd = q->AcquireCommandBuffer();
            cmd->Begin();
            // [indices][aabbIndices] block is unchanged: copy [0 .. m_verticesOffset).
            if (m_verticesOffset > 0)
                cmd->CopyBuffer(m_buffer, newBuf, m_verticesOffset, 0, 0);
            if (origVerticesBytes > 0)
                cmd->CopyBuffer(m_buffer, newBuf, origVerticesBytes, m_verticesOffset, newVerticesOffset);
            if (origPositionsBytes > 0)
                cmd->CopyBuffer(m_buffer, newBuf, origPositionsBytes, m_positionsOffset, newPositionsOffset);
            if (origAabbVertBytes > 0)
                cmd->CopyBuffer(m_buffer, newBuf, origAabbVertBytes, m_aabbVerticesOffset, newAabbVerticesOffset);
            cmd->End();
            q->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            cmd->Return();
        }

        RHII.AddToDeletionQueue([b = m_buffer]()
                                { Buffer *buf = b; Buffer::Destroy(buf); });
        m_buffer = newBuf;

        // Publish the new region offsets so GBuffer/DepthPass bind the shifted streams.
        m_positionsOffset = newPositionsOffset;
        m_aabbVerticesOffset = newAabbVerticesOffset;

        // Arena bookkeeping: shared vertex index base + per-stream byte bases derived from it.
        m_arenaVertexBase = sharedVertexBase; // first arena vertex index (both streams)
        m_arenaVertexCapacity = arenaVertCap; // in vertices
        m_arenaVertexUsed = 0;
        m_arenaIdxByteBase = arenaIdxByteBase; // arena index tail (bytes)
        m_arenaIdxCapacity = static_cast<size_t>(arenaIdxCap) * idxStride;
        m_arenaIdxUsed = 0;
        // Arena meshes occupy the TAIL slots [m_arenaSlotBase, m_meshCount). Regular meshes never
        // move (and MUST NOT trigger a geometry-dirty rebuild after this point — that destroys the
        // arena). The CPU shadow grows/shrinks with AddArenaMesh/RemoveArenaMesh.
        m_arenaSlotBase = m_meshCount;
        m_arenaSlots.clear();

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

        return 0;
    }

    int Scene::AddArenaMesh(uint32_t vertexIndex, size_t idxByteOffset,
                            const std::vector<Vertex> &verts,
                            const std::vector<PositionUvVertex> &posUv,
                            const std::vector<uint32_t> &indices, const AABB &localBox,
                            uint32_t reuseDataOffset, const MeshRuntime &runtimeForImages,
                            CommandBuffer *externalCmd)
    {
        if (!m_buffer || !m_indirectAll || !m_meshConstants || !m_visibility)
            return -1;

        const uint32_t vertCount = static_cast<uint32_t>(verts.size());
        if (posUv.size() != verts.size())
        {
            PE_WARN("Scene::AddArenaMesh: verts/posUv count mismatch (%zu vs %zu)", verts.size(), posUv.size());
            return -1;
        }

        const size_t vtxBytes = verts.size() * sizeof(Vertex);
        const size_t idxBytes = indices.size() * sizeof(uint32_t);
        const size_t posUvBytes = posUv.size() * sizeof(PositionUvVertex);

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
        if (m_meshCount >= m_indirectCapacity)
        {
            PE_WARN("Scene::AddArenaMesh: indirect capacity exceeded (meshCount=%u cap=%u)",
                    m_meshCount, m_indirectCapacity);
            return -1;
        }

        const int idx = static_cast<int>(m_meshCount);
        Queue *q = RHII.GetMainQueue();

        // Shared vertex index across BOTH streams -> a single vertexOffset is valid for the GBuffer
        // (Vertex stream @ m_verticesOffset) AND the depth prepass (PositionUvVertex stream @
        // m_positionsOffset). This is the invariant the naive tail-append violated.
        const size_t vtxByteOff = m_verticesOffset + static_cast<size_t>(vertexIndex) * sizeof(Vertex);
        const size_t posUvByteOff = m_positionsOffset + static_cast<size_t>(vertexIndex) * sizeof(PositionUvVertex);

        // Index buffer is bound at byte 0 -> firstIndex = byteOff/4. vertexOffset = the shared vertex
        // index (valid for both vertex streams).
        const uint32_t firstIndex = static_cast<uint32_t>(idxByteOffset / sizeof(uint32_t));
        const int32_t vertexOffset = static_cast<int32_t>(vertexIndex);

        PeDrawIndexedIndirectCommand drawCmd{};
        drawCmd.indexCount = static_cast<uint32_t>(indices.size());
        drawCmd.instanceCount = 1;
        drawCmd.firstIndex = firstIndex;
        drawCmd.vertexOffset = vertexOffset;
        drawCmd.firstInstance = static_cast<uint32_t>(idx);

        // Record the geometry/indirect/visibility GPU work. When externalCmd is supplied these copies
        // ride the caller's frame upload buffer (submitted on the main queue BEFORE the cull dispatch,
        // so no CPU stall); otherwise a transient command buffer is acquired/submitted/waited.
        auto recordGeometry = [&](CommandBuffer *cmd)
        {
            cmd->CopyBufferStaged(m_buffer, const_cast<Vertex *>(verts.data()), vtxBytes, vtxByteOff);
            {
                BufferBarrierInfo b{};
                b.buffer = m_buffer;
                b.stageMask = PE_STAGE_VERTEX_INPUT;
                b.accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ;
                b.offset = vtxByteOff;
                b.size = vtxBytes;
                cmd->BufferBarrier(b);
            }

            cmd->CopyBufferStaged(m_buffer, const_cast<PositionUvVertex *>(posUv.data()), posUvBytes, posUvByteOff);
            {
                BufferBarrierInfo b{};
                b.buffer = m_buffer;
                b.stageMask = PE_STAGE_VERTEX_INPUT;
                b.accessMask = PE_ACCESS_VERTEX_ATTRIBUTE_READ;
                b.offset = posUvByteOff;
                b.size = posUvBytes;
                cmd->BufferBarrier(b);
            }

            cmd->CopyBufferStaged(m_buffer, const_cast<uint32_t *>(indices.data()), idxBytes, idxByteOffset);
            {
                BufferBarrierInfo b{};
                b.buffer = m_buffer;
                b.stageMask = PE_STAGE_VERTEX_INPUT;
                b.accessMask = PE_ACCESS_INDEX_READ;
                b.offset = idxByteOffset;
                b.size = idxBytes;
                cmd->BufferBarrier(b);
            }

            cmd->CopyBufferStaged(m_indirectAll, &drawCmd, PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
                                  static_cast<size_t>(idx) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
            {
                BufferBarrierInfo b{};
                b.buffer = m_indirectAll;
                b.stageMask = PE_STAGE_DRAW_INDIRECT | PE_STAGE_COMPUTE_SHADER;
                b.accessMask = PE_ACCESS_INDIRECT_COMMAND_READ | PE_ACCESS_SHADER_READ;
                b.offset = static_cast<size_t>(idx) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
                b.size = PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
                cmd->BufferBarrier(b);
            }

            cmd->FillBuffer(m_visibility, static_cast<size_t>(idx) * sizeof(uint32_t),
                            sizeof(uint32_t), 1u);
            {
                BufferBarrierInfo b{};
                b.buffer = m_visibility;
                b.stageMask = PE_STAGE_COMPUTE_SHADER;
                b.accessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE;
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
            mc.editorFlags = 0x4u;
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
                BufferBarrierInfo barrier{};
                barrier.buffer = m_meshConstantsDevice;
                barrier.stageMask = PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_SHADER;
                barrier.accessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ;
                barrier.offset = mcOffset;
                barrier.size = sizeof(Mesh_Constants);
                cmd->BufferBarrier(barrier);
                m_meshConstantsDevice->GetTrackInfo() = barrier;
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

        // CPU shadow (parallel to slots [m_arenaSlotBase, m_meshCount); relIdx == m_arenaSlots.size()).
        ArenaSlot slot{};
        slot.indexCount = drawCmd.indexCount;
        slot.firstIndex = firstIndex;
        slot.vertexOffset = vertexOffset;
        slot.idxByteOffset = idxByteOffset;
        slot.idxBytes = idxBytes;
        slot.vertexCount = vertCount;
        m_arenaSlots.push_back(slot);

        m_arenaVertexUsed += vertCount;
        m_arenaIdxUsed += idxBytes;
        ++m_meshCount;
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

        const uint32_t lastSlot = m_meshCount - 1;
        const uint32_t relIdx = static_cast<uint32_t>(idx) - m_arenaSlotBase;
        const uint32_t relLast = lastSlot - m_arenaSlotBase;
        const bool doMove = (static_cast<uint32_t>(idx) != lastSlot);
        const size_t mcStride = sizeof(Mesh_Constants);

        const ArenaSlot removed = m_arenaSlots[relIdx]; // copy: m_arenaSlots[relIdx] is overwritten below

        // Rebuild the relocated draw on the CPU (no GPU readback). firstInstance MUST become `idx`
        // because the culling and GBuffer shaders use firstInstance as the storage index into
        // mesh-constants / visibility.
        PeDrawIndexedIndirectCommand movedDraw{};
        if (doMove)
        {
            const ArenaSlot &moved = m_arenaSlots[relLast];
            movedDraw.indexCount = moved.indexCount;
            movedDraw.instanceCount = 1;
            movedDraw.firstIndex = moved.firstIndex;
            movedDraw.vertexOffset = moved.vertexOffset;
            movedDraw.firstInstance = static_cast<uint32_t>(idx);
        }

        Queue *q = RHII.GetMainQueue();
        auto record = [&](CommandBuffer *cmd)
        {
            // 1. Neuter the REMOVED mesh's index bytes. Decrementing m_meshCount and (for a move)
            // overwriting slot `idx` is NOT sufficient: the two-phase Hi-Z occlusion path copies each
            // emitted draw into a persistent filtered buffer that is NOT cleared per-frame, so a draw
            // captured while the removed mesh was live can keep drawing for a few frames. Zeroing its
            // index bytes makes any such stale copy a degenerate (no-op) draw. (The relocated mesh keeps
            // its own valid geometry, so a stale copy of IT renders a harmless 1-frame duplicate.)
            if (removed.idxBytes > 0)
            {
                cmd->FillBuffer(m_buffer, removed.idxByteOffset, removed.idxBytes, 0u);
                BufferBarrierInfo gb{};
                gb.buffer = m_buffer;
                gb.stageMask = PE_STAGE_VERTEX_INPUT;
                gb.accessMask = PE_ACCESS_INDEX_READ;
                gb.offset = removed.idxByteOffset;
                gb.size = removed.idxBytes;
                cmd->BufferBarrier(gb);
            }

            if (doMove)
            {
                // 2. Relocate the last slot's indirect entry into `idx` (firstInstance re-patched).
                cmd->CopyBufferStaged(m_indirectAll, &movedDraw, PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
                                      static_cast<size_t>(idx) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
                {
                    BufferBarrierInfo b{};
                    b.buffer = m_indirectAll;
                    b.stageMask = PE_STAGE_DRAW_INDIRECT | PE_STAGE_COMPUTE_SHADER;
                    b.accessMask = PE_ACCESS_INDIRECT_COMMAND_READ | PE_ACCESS_SHADER_READ;
                    b.offset = static_cast<size_t>(idx) * PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
                    b.size = PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
                    cmd->BufferBarrier(b);
                }

                // 3. Re-seed the relocated slot's visibility to 1 (visible-last-frame). This is NOT a
                // same-buffer copy from lastSlot: D3D12 forbids a subresource being COPY_SOURCE and
                // COPY_DEST at once, and a Vulkan self-copy would lack a source barrier against the
                // phase-2 cull write to Visibility[lastSlot]. Losing the exact temporal-occlusion bit for
                // one frame is cosmetic (it self-corrects, identical to a freshly-added mesh's seed).
                cmd->FillBuffer(m_visibility, static_cast<size_t>(idx) * sizeof(uint32_t),
                                sizeof(uint32_t), 1u);
                {
                    BufferBarrierInfo b{};
                    b.buffer = m_visibility;
                    b.stageMask = PE_STAGE_COMPUTE_SHADER;
                    b.accessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_WRITE;
                    b.offset = static_cast<size_t>(idx) * sizeof(uint32_t);
                    b.size = sizeof(uint32_t);
                    cmd->BufferBarrier(b);
                }

                // 4. DX12 mesh-constants mirror: publish the relocated slot from the (already CPU-relocated
                // + flushed) host m_meshConstants[idx] into device[idx]. A host->device copy (NOT a device
                // self-copy) mirrors the AddArenaMesh publish and avoids the COPY_SOURCE/COPY_DEST clash.
                if (m_meshConstantsDevice)
                {
                    cmd->CopyBuffer(m_meshConstants, m_meshConstantsDevice, mcStride,
                                    static_cast<size_t>(idx) * mcStride,
                                    static_cast<size_t>(idx) * mcStride);
                    BufferBarrierInfo b{};
                    b.buffer = m_meshConstantsDevice;
                    b.stageMask = PE_STAGE_COMPUTE_SHADER | PE_STAGE_VERTEX_SHADER;
                    b.accessMask = PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_STORAGE_READ;
                    b.offset = static_cast<size_t>(idx) * mcStride;
                    b.size = mcStride;
                    cmd->BufferBarrier(b);
                    m_meshConstantsDevice->GetTrackInfo() = b;
                }
            }
        };

        // The vacated last slot needs no zeroing: m_meshCount is decremented so it is no longer
        // dispatched, and it still references the relocated mesh's VALID geometry (harmless).
        if (doMove)
        {
            // Relocate the host-side mesh constants (Vulkan reads these directly). Map+copy the moved
            // slot down into `idx` BEFORE record() runs, so step 4's host->device mirror publish reads
            // the updated host[idx].
            m_meshConstants->Map();
            uint8_t *base = static_cast<uint8_t *>(m_meshConstants->Data());
            if (base)
            {
                // Typed POD assignment (avoids a <cstring> dependency for memcpy).
                *reinterpret_cast<Mesh_Constants *>(base + static_cast<size_t>(idx) * mcStride) =
                    *reinterpret_cast<const Mesh_Constants *>(base + static_cast<size_t>(lastSlot) * mcStride);
                m_meshConstants->Flush(mcStride, static_cast<size_t>(idx) * mcStride);
            }
            m_meshConstants->Unmap();
        }

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

        // Update the CPU shadow (swap-remove) + counters.
        if (doMove)
            m_arenaSlots[relIdx] = m_arenaSlots[relLast];
        m_arenaSlots.pop_back();
        m_arenaVertexUsed -= removed.vertexCount;
        m_arenaIdxUsed -= removed.idxBytes;
        --m_meshCount;
        m_geometryVersion++;

        // The relocated mesh moved from slot `lastSlot` to `idx`: the caller fixes its handle->slot map.
        return doMove ? static_cast<int>(lastSlot) : -1;
    }
} // namespace pe
