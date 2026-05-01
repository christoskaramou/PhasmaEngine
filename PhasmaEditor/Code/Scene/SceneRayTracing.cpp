#include "Scene/Scene.h"
#include "API/AccelerationStructure.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/Vertex.h"

namespace pe
{
    void Scene::BuildAllBLASes(CommandBuffer *cmd)
    {
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

        vk::DeviceAddress bufferAddress = GetBuffer()->GetDeviceAddress();

        // --- Pass 1: Calculate BLAS sizes ---
        vk::DeviceSize totalBlasSize = 0;
        vk::DeviceSize maxScratchSize = 0;

        struct BlasBuildReq
        {
            vk::AccelerationStructureGeometryKHR geometry;
            vk::AccelerationStructureBuildRangeInfoKHR range;
            vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
            int meshIndex;
            AccelerationStructure *createdBlas = nullptr;
        };
        std::vector<BlasBuildReq> buildReqs;
        buildReqs.reserve(m_meshes.size());

        static constexpr vk::BuildAccelerationStructureFlagsKHR kBlasFlags =
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

        bool hasSkeleton = GetSkeleton().GetBoneCount() > 0;

        for (int meshIndex = 0; meshIndex < static_cast<int>(m_meshes.size()); meshIndex++)
        {
            const Mesh &mesh = m_meshes[meshIndex];
            if (mesh.indexCount == 0)
                continue;
            if (hasSkeleton && mesh.skinned)
                continue;

            vk::AccelerationStructureGeometryKHR geometry{};
            geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
            geometry.geometry.triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
            geometry.geometry.triangles.vertexData.deviceAddress =
                bufferAddress + m_verticesOffset + mesh.vertexOffset * sizeof(Vertex);
            geometry.geometry.triangles.vertexStride = sizeof(Vertex);
            geometry.geometry.triangles.maxVertex = mesh.vertexCount ? mesh.vertexCount - 1 : 0;
            geometry.geometry.triangles.indexType = vk::IndexType::eUint32;
            geometry.geometry.triangles.indexData.deviceAddress =
                bufferAddress + mesh.indexOffset * sizeof(uint32_t);
            if (mesh.renderType == RenderType::AlphaCut || mesh.renderType == RenderType::AlphaBlend ||
                mesh.renderType == RenderType::Transmission)
            {
                geometry.flags = vk::GeometryFlagBitsKHR::eNoDuplicateAnyHitInvocation;
            }
            else
            {
                geometry.flags = vk::GeometryFlagBitsKHR::eOpaque;
            }

            vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
            rangeInfo.primitiveCount = mesh.indexCount / 3;
            rangeInfo.primitiveOffset = 0;
            rangeInfo.firstVertex = 0;
            rangeInfo.transformOffset = 0;

            auto sizeInfo = AccelerationStructure::GetBuildSizes(
                {geometry},
                {rangeInfo.primitiveCount},
                vk::AccelerationStructureTypeKHR::eBottomLevel,
                kBlasFlags,
                vk::AccelerationStructureBuildTypeKHR::eDevice);

            totalBlasSize = RHII.Align(totalBlasSize + sizeInfo.accelerationStructureSize, 256);
            maxScratchSize = std::max(maxScratchSize, sizeInfo.buildScratchSize);

            buildReqs.push_back({geometry, rangeInfo, sizeInfo, meshIndex});
        }

        if (buildReqs.empty())
            return;

        vk::PhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
        vk::PhysicalDeviceProperties2 props{};
        props.pNext = &asProps;
        RHII.GetGpu().getProperties2(&props);
        auto scratchAlign = asProps.minAccelerationStructureScratchOffsetAlignment;

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
        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                               vk::PipelineStageFlagBits2::eTransfer;
        barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                                vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eTransferRead;
        cmd->MemoryBarrier(barrier);

        vk::DeviceSize currentOffset = 0;
        for (auto &req : buildReqs)
        {
            currentOffset = RHII.Align(currentOffset, 256);

            std::string name = "BLAS_mesh" + std::to_string(req.meshIndex);
            req.createdBlas = new AccelerationStructure(name, m_blasMergedBuffer, currentOffset);
            req.createdBlas->BuildBLAS(cmd, {req.geometry}, {req.range}, {req.range.primitiveCount},
                                       kBlasFlags, m_scratchBuffer->GetDeviceAddress());
            m_blases.push_back(req.createdBlas);
            m_blasByMesh[req.meshIndex] = req.createdBlas;

            currentOffset += req.sizeInfo.accelerationStructureSize;
        }
    }

    void Scene::BuildTLASFromInstances(CommandBuffer *cmd)
    {
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
            uint32_t nodeIndex;
            uint32_t meshSlot;
            mat4 transform;
        };
        std::vector<InstanceReq> instanceReqs;
        instanceReqs.reserve(m_meshCount);

        bool hasSkeleton = GetSkeleton().GetBoneCount() > 0;

        for (uint32_t i = 0; i < GetNodeCount(); i++)
        {
            if (m_nodeRuntime[i].gpuPending)
                continue;

            const auto &refs = m_nodeComponentCache[i].meshRefs->meshRefs;
            for (uint32_t slot = 0; slot < static_cast<uint32_t>(refs.size()); slot++)
            {
                int meshIdx = refs[slot];
                if (meshIdx < 0)
                    continue;
                if (m_meshes[meshIdx].indexCount == 0)
                    continue;
                if (hasSkeleton && m_meshes[meshIdx].skinned)
                    continue;

                auto it = m_blasByMesh.find(meshIdx);
                if (it == m_blasByMesh.end())
                    continue; // mesh has no BLAS (e.g., removed mesh, dead entry)

                instanceReqs.push_back({it->second, meshIdx, i, slot, m_nodeRuntime[i].gpuData.worldMatrix});
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
        static constexpr vk::BuildAccelerationStructureFlagsKHR kTlasFlags =
            vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

        vk::AccelerationStructureGeometryKHR tlasGeom{};
        tlasGeom.geometryType = vk::GeometryTypeKHR::eInstances;
        vk::AccelerationStructureGeometryInstancesDataKHR instData{};
        instData.arrayOfPointers = VK_FALSE;
        instData.data.deviceAddress = 0;
        tlasGeom.geometry.instances = instData;

        // Must include eAllowUpdate here — UpdateTLASTransformations needs updateScratchSize bytes,
        // which can exceed buildScratchSize on some hardware at high instance counts.
        auto tlasSizes = AccelerationStructure::GetBuildSizes(
            {tlasGeom},
            {m_rtInstanceCount},
            vk::AccelerationStructureTypeKHR::eTopLevel,
            kTlasFlags | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            vk::AccelerationStructureBuildTypeKHR::eDevice);

        vk::PhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
        vk::PhysicalDeviceProperties2 props{};
        props.pNext = &asProps;
        RHII.GetGpu().getProperties2(&props);
        auto scratchAlign = asProps.minAccelerationStructureScratchOffsetAlignment;
        vk::DeviceSize tlasScratch =
            std::max(tlasSizes.buildScratchSize, tlasSizes.updateScratchSize);

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
            .size = std::max((size_t)1, (size_t)m_rtInstanceCount) *
                    sizeof(vk::AccelerationStructureInstanceKHR),
            .usage = PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR |
                     PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS |
                     PE_BUFFER_USAGE_TRANSFER_DST,
            .memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU,
            .name = "TLAS_Instance_Buffer",
        });

        m_instanceBuffer->Map();
        auto *gpuInstances = (vk::AccelerationStructureInstanceKHR *)m_instanceBuffer->Data();

        for (size_t i = 0; i < instanceReqs.size(); i++)
        {
            auto &req = instanceReqs[i];

            vk::TransformMatrixKHR transformMatrix;
            mat4 &t = req.transform;
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

            const Mesh &mesh = m_meshes[req.meshIndex];
            bool isTransparent = (mesh.renderType == RenderType::AlphaBlend ||
                                  mesh.renderType == RenderType::Transmission ||
                                  mesh.renderType == RenderType::AlphaCut);

            gpuInstances[i].transform = transformMatrix;
            gpuInstances[i].instanceCustomIndex = static_cast<uint32_t>(i);
            gpuInstances[i].mask = isTransparent ? 0x80 : 0x01;
            gpuInstances[i].instanceShaderBindingTableRecordOffset = 0;
            gpuInstances[i].flags = static_cast<VkGeometryInstanceFlagBitsKHR>(
                vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);
            gpuInstances[i].accelerationStructureReference = req.blas->GetDeviceAddress();

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
            meshInfoGPU.renderType = static_cast<uint32_t>(mesh.renderType);

            for (int k = 0; k < 5; k++)
                meshInfoGPU.textures[k] = static_cast<int32_t>(meshRt.imageViewIndices[k]);
        }
        m_meshInfoBuffer->Flush();
        m_meshInfoBuffer->Unmap();
    }

    void Scene::BuildAccelerationStructures(CommandBuffer *cmd)
    {
        BuildAllBLASes(cmd);
        BuildTLASFromInstances(cmd);
    }

    void Scene::RebuildTLASOnly()
    {
        if (m_blasByMesh.empty())
            return;

        CommandBuffer *cmd = RHII.GetMainQueue()->AcquireCommandBuffer();
        cmd->Begin();

        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
        barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR;
        cmd->MemoryBarrier(barrier);

        BuildTLASFromInstances(cmd);

        cmd->End();
        RHII.GetMainQueue()->Submit(1, &cmd, nullptr, nullptr);
        cmd->Wait();
        cmd->Return();
    }

    void Scene::UpdateTLASTransformations(CommandBuffer *cmd)
    {
        if (!m_tlas || !m_instanceBuffer || !m_scratchBuffer)
            return;

        if (m_nodesMoved.empty())
            return;

        m_instanceBuffer->Map();
        auto *gpuInstances = (vk::AccelerationStructureInstanceKHR *)m_instanceBuffer->Data();

        for (NodeId *node : m_nodesMoved)
        {
            const NodeRuntime &rt = m_nodeRuntime[node->index];
            const mat4 &t = rt.gpuData.worldMatrix;

            vk::TransformMatrixKHR transformMatrix;
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

            for (int instanceIndex : rt.rtInstanceIndices)
            {
                if (instanceIndex >= 0 && instanceIndex < static_cast<int>(m_rtInstanceCount))
                    gpuInstances[instanceIndex].transform = transformMatrix;
            }
        }

        m_instanceBuffer->Flush();
        m_instanceBuffer->Unmap();

        m_tlas->UpdateTLAS(cmd, m_rtInstanceCount, m_instanceBuffer, m_scratchBuffer->GetDeviceAddress());
    }
} // namespace pe
