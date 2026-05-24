#include "Scene/Scene.h"
#include "API/Vulkan/AccelerationStructure.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vertex.h"

#if defined(PE_WIN32)
#include "API/DX12/Dx12AccelerationStructure.h"
#undef MemoryBarrier
#endif

namespace pe
{
    namespace
    {
        bool SupportsSceneRayTracing()
        {
            const PeGraphicsApi api = RHII.GetApi();
            return RHII.GetCaps().rayTracing &&
                   (api == PE_GRAPHICS_API_VULKAN || api == PE_GRAPHICS_API_DX12);
        }

        bool IsVulkanSceneRayTracing()
        {
            return RHII.GetApi() == PE_GRAPHICS_API_VULKAN;
        }

        bool IsDx12SceneRayTracing()
        {
            return RHII.GetApi() == PE_GRAPHICS_API_DX12;
        }

        uint64_t GetSceneRtScratchAlignment()
        {
            if (IsVulkanSceneRayTracing())
            {
                vk::PhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
                vk::PhysicalDeviceProperties2 props{};
                props.pNext = &asProps;
                VulkanRhi::Gpu().getProperties2(&props);
                return asProps.minAccelerationStructureScratchOffsetAlignment;
            }

#if defined(PE_WIN32)
            if (IsDx12SceneRayTracing())
                return D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT;
#endif

            return 256;
        }

        void FillVulkanTransform(vk::TransformMatrixKHR &transformMatrix, const mat4 &t)
        {
            transformMatrix.matrix[0][0] = t[0][0];
            transformMatrix.matrix[0][1] = t[1][0];
            transformMatrix.matrix[0][2] = t[2][0];
            transformMatrix.matrix[0][3] = t[3][0];
            transformMatrix.matrix[1][0] = t[0][1];
            transformMatrix.matrix[1][1] = t[1][1];
            transformMatrix.matrix[1][2] = t[2][1];
            transformMatrix.matrix[1][3] = t[3][1];
            transformMatrix.matrix[2][0] = t[0][2];
            transformMatrix.matrix[2][1] = t[1][2];
            transformMatrix.matrix[2][2] = t[2][2];
            transformMatrix.matrix[2][3] = t[3][2];
        }

#if defined(PE_WIN32)
        void FillDx12Transform(float (&transform)[3][4], const mat4 &t)
        {
            transform[0][0] = t[0][0];
            transform[0][1] = t[1][0];
            transform[0][2] = t[2][0];
            transform[0][3] = t[3][0];
            transform[1][0] = t[0][1];
            transform[1][1] = t[1][1];
            transform[1][2] = t[2][1];
            transform[1][3] = t[3][1];
            transform[2][0] = t[0][2];
            transform[2][1] = t[1][2];
            transform[2][2] = t[2][2];
            transform[2][3] = t[3][2];
        }
#endif
    } // namespace

    void Scene::BuildAllBLASes(CommandBuffer *cmd)
    {
        if (!SupportsSceneRayTracing())
            return;

        // Cleanup old BLAS resources
        for (auto *blas : m_blases)
            RHII.AddToDeletionQueue([blas]()
                                    { AccelerationStructure *b = blas; AccelerationStructure::Destroy(b); });
        m_blases.clear();
        m_blasByMesh.clear();

        RHII.AddToDeletionQueue([b = m_blasMergedBuffer]()
                                { Buffer *buf = b; Buffer::Destroy(buf); });
        m_blasMergedBuffer = nullptr;

        RHII.AddToDeletionQueue([b = m_scratchBuffer]()
                                { Buffer *buf = b; Buffer::Destroy(buf); });
        m_scratchBuffer = nullptr;

        if (!GetBuffer())
            return;
        if (m_meshCount == 0)
            return;

        uint64_t bufferAddress = GetBuffer()->GetDeviceAddress();

        // --- Pass 1: Calculate BLAS sizes ---
        uint64_t totalBlasSize = 0;
        uint64_t maxScratchSize = 0;

        struct BlasBuildReq
        {
            vk::AccelerationStructureGeometryKHR geometry;
            vk::AccelerationStructureBuildRangeInfoKHR range;
            vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
#if defined(PE_WIN32)
            Dx12RtTriangleGeometry dxGeometry;
#endif
            uint64_t resultSize = 0;
            int meshIndex;
            AccelerationStructure *createdBlas = nullptr;
        };
        std::vector<BlasBuildReq> buildReqs;
        buildReqs.reserve(m_meshes.size());

        static constexpr PeAccelerationStructureBuildFlags kBlasFlags =
            PE_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_TRACE;

        bool hasSkeleton = GetSkeleton().GetBoneCount() > 0;

        for (int meshIndex = 0; meshIndex < static_cast<int>(m_meshes.size()); meshIndex++)
        {
            const Mesh &mesh = m_meshes[meshIndex];
            if (mesh.indexCount == 0)
                continue;
            if (hasSkeleton && mesh.skinned)
                continue;

            BlasBuildReq req{};
            req.meshIndex = meshIndex;
            const bool isTransparent = mesh.renderType == RenderType::AlphaCut ||
                                       mesh.renderType == RenderType::AlphaBlend ||
                                       mesh.renderType == RenderType::Transmission;

            if (IsVulkanSceneRayTracing())
            {
                req.geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
                req.geometry.geometry.triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
                req.geometry.geometry.triangles.vertexData.deviceAddress =
                    bufferAddress + m_verticesOffset + mesh.vertexOffset * sizeof(Vertex);
                req.geometry.geometry.triangles.vertexStride = sizeof(Vertex);
                req.geometry.geometry.triangles.maxVertex = mesh.vertexCount ? mesh.vertexCount - 1 : 0;
                req.geometry.geometry.triangles.indexType = vk::IndexType::eUint32;
                req.geometry.geometry.triangles.indexData.deviceAddress =
                    bufferAddress + mesh.indexOffset * sizeof(uint32_t);
                req.geometry.flags = isTransparent
                                         ? vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation
                                         : vk::GeometryFlagBitsKHR::eOpaque;

                req.range.primitiveCount = mesh.indexCount / 3;
                req.range.primitiveOffset = 0;
                req.range.firstVertex = 0;
                req.range.transformOffset = 0;

                req.sizeInfo = GetVulkanAccelerationStructureBuildSizes(
                    {req.geometry},
                    {req.range.primitiveCount},
                    vk::AccelerationStructureTypeKHR::eBottomLevel,
                    kBlasFlags,
                    vk::AccelerationStructureBuildTypeKHR::eDevice);

                req.resultSize = req.sizeInfo.accelerationStructureSize;
                maxScratchSize = std::max<uint64_t>(maxScratchSize, req.sizeInfo.buildScratchSize);
            }

#if defined(PE_WIN32)
            if (IsDx12SceneRayTracing())
            {
                req.dxGeometry.vertexAddress = bufferAddress + m_verticesOffset + mesh.vertexOffset * sizeof(Vertex);
                req.dxGeometry.vertexCount = mesh.vertexCount;
                req.dxGeometry.vertexStride = sizeof(Vertex);
                req.dxGeometry.vertexFormat = PE_FORMAT_R32G32B32_SFLOAT;
                req.dxGeometry.indexAddress = bufferAddress + mesh.indexOffset * sizeof(uint32_t);
                req.dxGeometry.indexCount = mesh.indexCount;
                req.dxGeometry.indexType = PE_INDEX_TYPE_UINT32;
                req.dxGeometry.opaque = !isTransparent;

                Dx12RtPrebuildInfo sizeInfo = GetDx12BlasPrebuildInfo({req.dxGeometry}, kBlasFlags);
                req.resultSize = sizeInfo.resultDataMaxSize;
                maxScratchSize = std::max<uint64_t>(maxScratchSize, sizeInfo.buildScratchSize);
            }
#endif

            PE_ERROR_IF(req.resultSize == 0, "Scene::BuildAllBLASes: mesh %d produced zero BLAS size", meshIndex);
            totalBlasSize = RHII.Align(totalBlasSize + req.resultSize, 256);
            buildReqs.push_back(req);
        }

        if (buildReqs.empty())
            return;

        const uint64_t scratchAlign = GetSceneRtScratchAlignment();

        m_blasMergedBuffer = Buffer::Create({
            .size = totalBlasSize,
            .usage = PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_KHR | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = "BLAS_Merged_Buffer",
        });

        m_scratchBuffer = Buffer::Create({
            .size = RHII.Align(maxScratchSize, scratchAlign),
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = "AS_Scratch_Buffer",
        });

        // --- Pass 2: Build BLASes ---
        MemoryBarrierInfo barrier{};
        barrier.srcStageMask = PE_STAGE_TRANSFER;
        barrier.srcAccessMask = PE_ACCESS_TRANSFER_WRITE;
        barrier.dstStageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR |
                               PE_STAGE_TRANSFER;
        barrier.dstAccessMask = PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR |
                                PE_ACCESS_SHADER_READ | PE_ACCESS_TRANSFER_READ;
        cmd->MemoryBarrier(barrier);

#if defined(PE_WIN32)
        if (IsDx12SceneRayTracing())
        {
            BufferBarrierInfo geometryBarrier{};
            geometryBarrier.buffer = GetBuffer();
            geometryBarrier.stageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
            geometryBarrier.accessMask = PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR;
            cmd->BufferBarrier(geometryBarrier);
        }
#endif

        uint64_t currentOffset = 0;
        for (auto &req : buildReqs)
        {
            currentOffset = RHII.Align(currentOffset, 256);

            std::string name = "BLAS_mesh" + std::to_string(req.meshIndex);
            req.createdBlas = new AccelerationStructure(name, m_blasMergedBuffer, currentOffset);
            if (IsVulkanSceneRayTracing())
            {
                BuildVulkanBLAS(req.createdBlas, cmd, {req.geometry}, {req.range}, {req.range.primitiveCount},
                                kBlasFlags, m_scratchBuffer->GetDeviceAddress());
            }
#if defined(PE_WIN32)
            else if (IsDx12SceneRayTracing())
            {
                BuildDx12BLAS(req.createdBlas, cmd, {req.dxGeometry}, kBlasFlags, m_scratchBuffer->GetDeviceAddress());
            }
#endif
            m_blases.push_back(req.createdBlas);
            m_blasByMesh[req.meshIndex] = req.createdBlas;

            currentOffset += req.resultSize;
        }
    }

    void Scene::BuildTLASFromInstances(CommandBuffer *cmd)
    {
        if (!SupportsSceneRayTracing())
            return;

        // Cleanup old TLAS resources (keep BLAS — caller manages those)
        RHII.AddToDeletionQueue([t = m_tlas]()
                                { AccelerationStructure *as = t; AccelerationStructure::Destroy(as); });
        RHII.AddToDeletionQueue([b = m_instanceBuffer]()
                                { Buffer *buf = b; Buffer::Destroy(buf); });
        m_tlas = nullptr;
        m_instanceBuffer = nullptr;

        if (m_blasByMesh.empty())
            return;

        // --- Collect instances ---
        struct InstanceReq
        {
            AccelerationStructure *blas;
            int meshIndex;
            uint32_t constantsIndex;
            uint32_t nodeIndex;
            uint32_t meshSlot;
            mat4 transform;
        };
        std::vector<InstanceReq> instanceReqs;
        instanceReqs.reserve(m_meshCount);

        bool hasSkeleton = GetSkeleton().GetBoneCount() > 0;
        uint32_t constantsIndex = 0;

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending || !IsNodeHierarchyEnabled(m_nodeIds[i]))
                continue;

            const auto &refs = m_nodeComponentCache[i].meshRefs->meshRefs;
            for (uint32_t slot = 0; slot < static_cast<uint32_t>(refs.size()); slot++)
            {
                int meshIdx = refs[slot];
                if (meshIdx < 0)
                    continue;
                if (m_meshes[meshIdx].indexCount == 0)
                    continue;

                const uint32_t currentConstantsIndex = constantsIndex++;
                if (hasSkeleton && m_meshes[meshIdx].skinned)
                    continue;

                auto it = m_blasByMesh.find(meshIdx);
                if (it == m_blasByMesh.end())
                    continue; // mesh has no BLAS (e.g., removed mesh, dead entry)

                instanceReqs.push_back({it->second, meshIdx, currentConstantsIndex, i, slot, m_nodeRuntime[i].gpuData.worldMatrix});
            }
        }

        m_rtInstanceCount = static_cast<uint32_t>(instanceReqs.size());

        if (m_rtInstanceCount == 0)
            return;

        // Reset per-node RT instance index tracking
        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            auto &rt = m_nodeRuntime[i];
            const auto &refs = m_nodeComponentCache[i].meshRefs->meshRefs;
            rt.rtInstanceIndices.assign(refs.size(), -1);
        }

        // --- Build TLAS size info ---
        static constexpr PeAccelerationStructureBuildFlags kTlasFlags =
            PE_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_TRACE;

        uint64_t tlasScratch = 0;
        if (IsVulkanSceneRayTracing())
        {
            vk::AccelerationStructureGeometryKHR tlasGeom{};
            tlasGeom.geometryType = vk::GeometryTypeKHR::eInstances;
            vk::AccelerationStructureGeometryInstancesDataKHR instData{};
            instData.arrayOfPointers = VK_FALSE;
            instData.data.deviceAddress = 0;
            tlasGeom.geometry.instances = instData;

            // Must include ALLOW_UPDATE here: UpdateTLASTransformations needs update scratch,
            // which can exceed build scratch on some hardware at high instance counts.
            auto tlasSizes = GetVulkanAccelerationStructureBuildSizes(
                {tlasGeom},
                {m_rtInstanceCount},
                vk::AccelerationStructureTypeKHR::eTopLevel,
                kTlasFlags | PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_UPDATE,
                vk::AccelerationStructureBuildTypeKHR::eDevice);
            tlasScratch = std::max<uint64_t>(tlasSizes.buildScratchSize, tlasSizes.updateScratchSize);
        }
#if defined(PE_WIN32)
        else if (IsDx12SceneRayTracing())
        {
            Dx12RtPrebuildInfo tlasSizes = GetDx12TlasPrebuildInfo(
                m_rtInstanceCount,
                kTlasFlags | PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_UPDATE);
            tlasScratch = std::max<uint64_t>(tlasSizes.buildScratchSize, tlasSizes.updateScratchSize);
        }
#endif
        const uint64_t scratchAlign = GetSceneRtScratchAlignment();

        // Reallocate scratch buffer sized for TLAS (BLAS scratch was separate in BuildAllBLASes)
        RHII.AddToDeletionQueue([b = m_scratchBuffer]()
                                { Buffer *buf = b; Buffer::Destroy(buf); });
        m_scratchBuffer = Buffer::Create({
            .size = RHII.Align(tlasScratch, scratchAlign),
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = "AS_Scratch_Buffer",
        });

        // --- Build instance buffer ---
        m_instanceBuffer = Buffer::Create({
            .size = std::max<size_t>(1, m_rtInstanceCount) *
                    (IsVulkanSceneRayTracing() ? sizeof(vk::AccelerationStructureInstanceKHR)
#if defined(PE_WIN32)
                                               : sizeof(D3D12_RAYTRACING_INSTANCE_DESC)
#else
                                               : sizeof(vk::AccelerationStructureInstanceKHR)
#endif
                         ),
            .usage = PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR |
                     PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS |
                     PE_BUFFER_USAGE_TRANSFER_DST,
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
            .name = "TLAS_Instance_Buffer",
        });

        m_instanceBuffer->Map();
        auto *vkInstances = IsVulkanSceneRayTracing()
                                ? static_cast<vk::AccelerationStructureInstanceKHR *>(m_instanceBuffer->Data())
                                : nullptr;
#if defined(PE_WIN32)
        auto *dxInstances = IsDx12SceneRayTracing()
                                ? static_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(m_instanceBuffer->Data())
                                : nullptr;
#endif

        for (size_t i = 0; i < instanceReqs.size(); i++)
        {
            auto &req = instanceReqs[i];

            mat4 &t = req.transform;

            const Mesh &mesh = m_meshes[req.meshIndex];
            bool isTransparent = (mesh.renderType == RenderType::AlphaBlend ||
                                  mesh.renderType == RenderType::Transmission ||
                                  mesh.renderType == RenderType::AlphaCut);

            if (vkInstances)
            {
                vk::TransformMatrixKHR transformMatrix{};
                FillVulkanTransform(transformMatrix, t);
                vkInstances[i].transform = transformMatrix;
                vkInstances[i].instanceCustomIndex = static_cast<uint32_t>(i);
                vkInstances[i].mask = isTransparent ? 0x80 : 0x01;
                vkInstances[i].instanceShaderBindingTableRecordOffset = 0;
                vkInstances[i].flags = static_cast<VkGeometryInstanceFlagBitsKHR>(
                    vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
                vkInstances[i].accelerationStructureReference = req.blas->GetDeviceAddress();
            }
#if defined(PE_WIN32)
            if (dxInstances)
            {
                FillDx12Transform(dxInstances[i].Transform, t);
                dxInstances[i].InstanceID = static_cast<UINT>(i);
                dxInstances[i].InstanceMask = isTransparent ? 0x80 : 0x01;
                dxInstances[i].InstanceContributionToHitGroupIndex = 0;
                dxInstances[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
                dxInstances[i].AccelerationStructure = req.blas->GetDeviceAddress();
            }
#endif

            m_nodeRuntime[req.nodeIndex].rtInstanceIndices[req.meshSlot] = static_cast<int>(i);
        }
        m_instanceBuffer->Flush();
        m_instanceBuffer->Unmap();

        // --- Build TLAS ---
        m_tlas = AccelerationStructure::Create("TLAS", nullptr, 0);
        m_tlas->BuildTLAS(cmd, m_rtInstanceCount, m_instanceBuffer, kTlasFlags,
                          m_scratchBuffer->GetDeviceAddress());

        // --- Build MeshInfoGPU Buffer ---
        struct MeshInfoGPU
        {
            uint32_t indexOffset;
            uint32_t vertexOffset;
            uint32_t positionsOffset;
            uint32_t renderType;
            uint32_t constantsIndex;
            int32_t textures[5];
        };

        RHII.AddToDeletionQueue([b = m_meshInfoBuffer]()
                                { Buffer *buf = b; Buffer::Destroy(buf); });
        m_meshInfoBuffer = Buffer::Create({
            .size = std::max((size_t)1, (size_t)m_rtInstanceCount) * sizeof(MeshInfoGPU),
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_TRANSFER_DST,
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
            .name = "MeshInfo_Buffer",
        });

        m_meshInfoBuffer->Map();
        auto *meshInfosGPU = (MeshInfoGPU *)m_meshInfoBuffer->Data();

        for (size_t i = 0; i < instanceReqs.size(); i++)
        {
            auto &req = instanceReqs[i];
            auto &meshInfoGPU = meshInfosGPU[i];

            const Mesh &mesh = m_meshes[req.meshIndex];
            const MeshRuntime &meshRt = m_meshRuntimes[req.meshIndex];

            meshInfoGPU.indexOffset = mesh.indexOffset * 4;
            meshInfoGPU.vertexOffset =
                static_cast<uint32_t>(m_verticesOffset) + mesh.vertexOffset * sizeof(Vertex);
            meshInfoGPU.positionsOffset =
                static_cast<uint32_t>(m_positionsOffset) + mesh.positionsOffset * sizeof(PositionUvVertex);
            meshInfoGPU.renderType = static_cast<uint32_t>(mesh.renderType);
            meshInfoGPU.constantsIndex = req.constantsIndex;

            for (int k = 0; k < 5; k++)
                meshInfoGPU.textures[k] = static_cast<int32_t>(meshRt.imageViewIndices[k]);
        }
        m_meshInfoBuffer->Flush();
        m_meshInfoBuffer->Unmap();
    }

    void Scene::BuildAccelerationStructures(CommandBuffer *cmd)
    {
        if (!SupportsSceneRayTracing())
            return;

        BuildAllBLASes(cmd);
        BuildTLASFromInstances(cmd);
    }

    void Scene::RebuildTLASOnly()
    {
        if (!SupportsSceneRayTracing())
            return;

        if (m_blasByMesh.empty())
            return;

        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();
        cmd->Begin();

        MemoryBarrierInfo barrier{};
        barrier.srcStageMask = PE_STAGE_TRANSFER;
        barrier.srcAccessMask = PE_ACCESS_TRANSFER_WRITE;
        barrier.dstStageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
        barrier.dstAccessMask = PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR;
        cmd->MemoryBarrier(barrier);

        BuildTLASFromInstances(cmd);

        cmd->End();
        RHII.GetMainQueue()->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
    }

    void Scene::UpdateTLASTransformations(CommandBuffer *cmd)
    {
        if (!SupportsSceneRayTracing())
            return;

        if (!m_tlas || !m_instanceBuffer || !m_scratchBuffer)
            return;

        if (m_nodesMoved.empty())
            return;

        m_instanceBuffer->Map();
        auto *vkInstances = IsVulkanSceneRayTracing()
                                ? static_cast<vk::AccelerationStructureInstanceKHR *>(m_instanceBuffer->Data())
                                : nullptr;
#if defined(PE_WIN32)
        auto *dxInstances = IsDx12SceneRayTracing()
                                ? static_cast<D3D12_RAYTRACING_INSTANCE_DESC *>(m_instanceBuffer->Data())
                                : nullptr;
#endif

        for (NodeId *node : m_nodesMoved)
        {
            const NodeRuntime &rt = m_nodeRuntime[node->index];
            const mat4 &t = rt.gpuData.worldMatrix;

            for (int instanceIndex : rt.rtInstanceIndices)
            {
                if (instanceIndex >= 0 && instanceIndex < static_cast<int>(m_rtInstanceCount))
                {
                    if (vkInstances)
                    {
                        vk::TransformMatrixKHR transformMatrix{};
                        FillVulkanTransform(transformMatrix, t);
                        vkInstances[instanceIndex].transform = transformMatrix;
                    }
#if defined(PE_WIN32)
                    if (dxInstances)
                        FillDx12Transform(dxInstances[instanceIndex].Transform, t);
#endif
                }
            }
        }

        m_instanceBuffer->Flush();
        m_instanceBuffer->Unmap();

        m_tlas->UpdateTLAS(cmd, m_rtInstanceCount, m_instanceBuffer, m_scratchBuffer->GetDeviceAddress());
    }
} // namespace pe
