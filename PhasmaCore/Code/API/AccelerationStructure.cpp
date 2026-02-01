#include "API/AccelerationStructure.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/RHI.h"

namespace pe
{
    vk::AccelerationStructureBuildSizesInfoKHR AccelerationStructure::GetBuildSizes(
        const std::vector<vk::AccelerationStructureGeometryKHR> &geometries,
        const std::vector<uint32_t> &maxPrimitiveCounts,
        vk::AccelerationStructureTypeKHR accelerationStructureType,
        vk::BuildAccelerationStructureFlagsKHR flags,
        vk::AccelerationStructureBuildTypeKHR type)
    {
        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = accelerationStructureType;
        buildInfo.flags = flags;
        buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
        buildInfo.pGeometries = geometries.data();

        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
        RHII.GetDevice().getAccelerationStructureBuildSizesKHR(
            type,
            &buildInfo,
            maxPrimitiveCounts.data(),
            &sizeInfo);
        return sizeInfo;
    }

    AccelerationStructure::AccelerationStructure(const std::string &name, Buffer *buffer, uint64_t offset)
        : m_buffer(buffer), m_offset(offset), m_name(name)
    {
        if (m_buffer)
            m_externalBuffer = true;
    }

    AccelerationStructure::~AccelerationStructure()
    {
        if (m_apiHandle)
        {
            RHII.GetDevice().destroyAccelerationStructureKHR(m_apiHandle);
        }

        if (!m_externalBuffer)
            Buffer::Destroy(m_buffer);
        Buffer::Destroy(m_scratchBuffer);
    }

    void AccelerationStructure::CreateBuffer(size_t size)
    {
        m_buffer = Buffer::Create(
            size,
            vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            m_name + "_buffer");
    }

    void AccelerationStructure::CreateScratchBuffer(size_t size)
    {
        m_scratchBuffer = Buffer::Create(
            size,
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
            m_name + "_scratch_buffer");
    }

    void AccelerationStructure::BuildBLAS(CommandBuffer *cmd,
                                          const std::vector<vk::AccelerationStructureGeometryKHR> &geometries,
                                          const std::vector<vk::AccelerationStructureBuildRangeInfoKHR> &buildRanges,
                                          const std::vector<uint32_t> &maxPrimitiveCounts,
                                          vk::BuildAccelerationStructureFlagsKHR flags,
                                          vk::DeviceAddress scratchAddress)
    {
        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        buildInfo.flags = flags;
        buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
        buildInfo.pGeometries = geometries.data();

        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
        RHII.GetDevice().getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            &buildInfo,
            maxPrimitiveCounts.data(),
            &sizeInfo);

        if (!m_externalBuffer)
            CreateBuffer(sizeInfo.accelerationStructureSize);

        if (!scratchAddress)
        {
            CreateScratchBuffer(sizeInfo.buildScratchSize);
            buildInfo.scratchData.deviceAddress = m_scratchBuffer->GetDeviceAddress();
        }
        else
        {
            buildInfo.scratchData.deviceAddress = scratchAddress;
        }

        vk::AccelerationStructureCreateInfoKHR createInfo{};
        createInfo.buffer = m_buffer->ApiHandle();
        createInfo.offset = m_offset;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        m_apiHandle = RHII.GetDevice().createAccelerationStructureKHR(createInfo);

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.accelerationStructure = m_apiHandle;
        m_deviceAddress = RHII.GetDevice().getAccelerationStructureAddressKHR(&addressInfo);

        buildInfo.dstAccelerationStructure = m_apiHandle;

        const vk::AccelerationStructureBuildRangeInfoKHR *pBuildRanges = buildRanges.data();
        cmd->BuildAccelerationStructures(1, &buildInfo, &pBuildRanges);

        // Add barrier for subsequent use
        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
        barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR; // Block next build if using same scratch
        barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
        cmd->MemoryBarrier(barrier);
    }

    void AccelerationStructure::BuildTLAS(CommandBuffer *cmd,
                                          uint32_t instanceCount,
                                          Buffer *instanceBuffer,
                                          vk::BuildAccelerationStructureFlagsKHR flags,
                                          vk::DeviceAddress scratchAddress)
    {
        vk::AccelerationStructureGeometryInstancesDataKHR instancesVk{};
        instancesVk.data.deviceAddress = instanceBuffer->GetDeviceAddress();

        vk::AccelerationStructureGeometryKHR geometry{};
        geometry.geometryType = vk::GeometryTypeKHR::eInstances;
        geometry.geometry.instances = instancesVk;

        // Add eAllowUpdate to enable subsequent UpdateTLAS calls
        vk::BuildAccelerationStructureFlagsKHR buildFlags = flags | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        buildInfo.flags = buildFlags;
        buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
        uint32_t count = instanceCount;
        RHII.GetDevice().getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            &buildInfo,
            &count,
            &sizeInfo);

        if (!m_externalBuffer)
            CreateBuffer(sizeInfo.accelerationStructureSize);

        if (!scratchAddress)
        {
            CreateScratchBuffer(sizeInfo.buildScratchSize);
            buildInfo.scratchData.deviceAddress = m_scratchBuffer->GetDeviceAddress();
        }
        else
        {
            buildInfo.scratchData.deviceAddress = scratchAddress;
        }

        vk::AccelerationStructureCreateInfoKHR createInfo{};
        createInfo.buffer = m_buffer->ApiHandle();
        createInfo.offset = m_offset;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        m_apiHandle = RHII.GetDevice().createAccelerationStructureKHR(createInfo);

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.accelerationStructure = m_apiHandle;
        m_deviceAddress = RHII.GetDevice().getAccelerationStructureAddressKHR(&addressInfo);

        buildInfo.dstAccelerationStructure = m_apiHandle;

        vk::AccelerationStructureBuildRangeInfoKHR buildRange{};
        buildRange.primitiveCount = instanceCount;
        buildRange.primitiveOffset = 0;
        buildRange.firstVertex = 0;
        buildRange.transformOffset = 0;

        const vk::AccelerationStructureBuildRangeInfoKHR *pBuildRanges = &buildRange;
        cmd->BuildAccelerationStructures(1, &buildInfo, &pBuildRanges);

        // Add barrier for subsequent use
        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
        barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
        barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
        cmd->MemoryBarrier(barrier);
    }

    void AccelerationStructure::UpdateTLAS(CommandBuffer *cmd,
                                           uint32_t instanceCount,
                                           Buffer *instanceBuffer,
                                           vk::DeviceAddress scratchAddress)
    {
        if (!m_apiHandle)
            return; // Must have been built first

        vk::AccelerationStructureGeometryInstancesDataKHR instancesVk{};
        instancesVk.data.deviceAddress = instanceBuffer->GetDeviceAddress();

        vk::AccelerationStructureGeometryKHR geometry{};
        geometry.geometryType = vk::GeometryTypeKHR::eInstances;
        geometry.geometry.instances = instancesVk;

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        buildInfo.flags = vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate | vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
        buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eUpdate; // In-place update
        buildInfo.srcAccelerationStructure = m_apiHandle;
        buildInfo.dstAccelerationStructure = m_apiHandle;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        if (scratchAddress)
            buildInfo.scratchData.deviceAddress = scratchAddress;
        else if (m_scratchBuffer)
            buildInfo.scratchData.deviceAddress = m_scratchBuffer->GetDeviceAddress();

        vk::AccelerationStructureBuildRangeInfoKHR buildRange{};
        buildRange.primitiveCount = instanceCount;
        buildRange.primitiveOffset = 0;
        buildRange.firstVertex = 0;
        buildRange.transformOffset = 0;

        const vk::AccelerationStructureBuildRangeInfoKHR *pBuildRanges = &buildRange;
        cmd->BuildAccelerationStructures(1, &buildInfo, &pBuildRanges);

        // Add barrier for subsequent use
        vk::MemoryBarrier2 barrier{};
        barrier.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
        barrier.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
        barrier.dstStageMask = vk::PipelineStageFlagBits2::eRayTracingShaderKHR | vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
        barrier.dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
        cmd->MemoryBarrier(barrier);
    }

    uint64_t AccelerationStructure::GetDeviceAddress() const
    {
        return m_deviceAddress;
    }
} // namespace pe
