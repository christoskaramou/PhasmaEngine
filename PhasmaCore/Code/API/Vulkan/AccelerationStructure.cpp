#include "API/Vulkan/AccelerationStructure.h"

#include "API/Buffer.h"
#include "API/Command.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"

namespace pe
{
    namespace
    {
        vk::BuildAccelerationStructureFlagsKHR ToVkAccelerationStructureBuildFlags(PeAccelerationStructureBuildFlags flags)
        {
            vk::BuildAccelerationStructureFlagsKHR vkFlags{};
            if (flags & PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_UPDATE)
                vkFlags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate;
            if (flags & PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_COMPACTION)
                vkFlags |= vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction;
            if (flags & PE_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_TRACE)
                vkFlags |= vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
            if (flags & PE_ACCELERATION_STRUCTURE_BUILD_PREFER_FAST_BUILD)
                vkFlags |= vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild;
            if (flags & PE_ACCELERATION_STRUCTURE_BUILD_LOW_MEMORY)
                vkFlags |= vk::BuildAccelerationStructureFlagBitsKHR::eLowMemory;
            return vkFlags;
        }

        bool SupportsVulkanAccelerationStructures()
        {
            return RHII.GetApi() == PE_GRAPHICS_API_VULKAN && RHII.GetCaps().rayTracing;
        }
    } // namespace

    void BuildAccelerationStructures(CommandBuffer *cmd,
                                     uint32_t infoCount,
                                     const vk::AccelerationStructureBuildGeometryInfoKHR *pInfos,
                                     const vk::AccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
    {
        if (!SupportsVulkanAccelerationStructures())
        {
            PE_ERROR("Vulkan acceleration-structure build requested without Vulkan ray-tracing support");
            return;
        }

        GetVulkanCommandBuffer(cmd).buildAccelerationStructuresKHR(infoCount, pInfos, ppBuildRangeInfos);
    }

    vk::AccelerationStructureBuildSizesInfoKHR GetVulkanAccelerationStructureBuildSizes(
        const std::vector<vk::AccelerationStructureGeometryKHR> &geometries,
        const std::vector<uint32_t> &maxPrimitiveCounts,
        vk::AccelerationStructureTypeKHR accelerationStructureType,
        PeAccelerationStructureBuildFlags flags,
        vk::AccelerationStructureBuildTypeKHR type)
    {
        if (!SupportsVulkanAccelerationStructures())
        {
            PE_ERROR("Vulkan acceleration-structure sizing requested without Vulkan ray-tracing support");
            return {};
        }

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = accelerationStructureType;
        buildInfo.flags = ToVkAccelerationStructureBuildFlags(flags);
        buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
        buildInfo.pGeometries = geometries.data();

        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo;
        VulkanRhi::Device().getAccelerationStructureBuildSizesKHR(
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
            VulkanRhi::Device().destroyAccelerationStructureKHR(GetVulkanAccelerationStructure(this));
        }

        if (!m_externalBuffer)
            Buffer::Destroy(m_buffer);
        Buffer::Destroy(m_scratchBuffer);
    }

    void AccelerationStructure::CreateBuffer(size_t size)
    {
        m_buffer = Buffer::Create({
            .size = size,
            .usage = PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_KHR | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = m_name + "_buffer",
        });
    }

    void AccelerationStructure::CreateScratchBuffer(size_t size)
    {
        m_scratchBuffer = Buffer::Create({
            .size = size,
            .usage = PE_BUFFER_USAGE_STORAGE_BUFFER | PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS,
            .memoryUsage = PE_MEMORY_USAGE_GPU_ONLY_DEDICATED,
            .name = m_name + "_scratch_buffer",
        });
    }

    void BuildVulkanBLAS(AccelerationStructure *as,
                         CommandBuffer *cmd,
                         const std::vector<vk::AccelerationStructureGeometryKHR> &geometries,
                         const std::vector<vk::AccelerationStructureBuildRangeInfoKHR> &buildRanges,
                         const std::vector<uint32_t> &maxPrimitiveCounts,
                         PeAccelerationStructureBuildFlags flags,
                         vk::DeviceAddress scratchAddress)
    {
        if (!SupportsVulkanAccelerationStructures())
        {
            PE_ERROR("Vulkan BLAS build requested without Vulkan ray-tracing support");
            return;
        }

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        buildInfo.flags = ToVkAccelerationStructureBuildFlags(flags);
        buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = static_cast<uint32_t>(geometries.size());
        buildInfo.pGeometries = geometries.data();

        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
        VulkanRhi::Device().getAccelerationStructureBuildSizesKHR(
            vk::AccelerationStructureBuildTypeKHR::eDevice,
            &buildInfo,
            maxPrimitiveCounts.data(),
            &sizeInfo);

        if (!AccelerationStructureAccess::IsExternalBuffer(as))
            AccelerationStructureAccess::CreateBuffer(as, sizeInfo.accelerationStructureSize);

        if (!scratchAddress)
        {
            AccelerationStructureAccess::CreateScratchBuffer(as, sizeInfo.buildScratchSize);
            buildInfo.scratchData.deviceAddress = AccelerationStructureAccess::GetScratchBuffer(as)->GetDeviceAddress();
        }
        else
        {
            buildInfo.scratchData.deviceAddress = scratchAddress;
        }

        vk::AccelerationStructureCreateInfoKHR createInfo{};
        createInfo.buffer = GetVulkanBuffer(AccelerationStructureAccess::GetBuffer(as));
        createInfo.offset = AccelerationStructureAccess::GetOffset(as);
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        vk::AccelerationStructureKHR apiHandle = VulkanRhi::Device().createAccelerationStructureKHR(createInfo);
        as->ApiHandle() = detail::ToUintPtr(apiHandle);

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.accelerationStructure = apiHandle;
        AccelerationStructureAccess::SetDeviceAddress(
            as, VulkanRhi::Device().getAccelerationStructureAddressKHR(&addressInfo));

        buildInfo.dstAccelerationStructure = apiHandle;

        const vk::AccelerationStructureBuildRangeInfoKHR *pBuildRanges = buildRanges.data();
        BuildAccelerationStructures(cmd, 1, &buildInfo, &pBuildRanges);

        // Add barrier for subsequent use
        MemoryBarrierInfo barrier{};
        barrier.srcStageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
        barrier.srcAccessMask = PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
        barrier.dstStageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR; // Block next build if using same scratch
        barrier.dstAccessMask = PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR | PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
        cmd->MemoryBarrier(barrier);
    }

    void AccelerationStructure::BuildTLAS(CommandBuffer *cmd,
                                          uint32_t instanceCount,
                                          Buffer *instanceBuffer,
                                          PeAccelerationStructureBuildFlags flags,
                                          uint64_t scratchAddress)
    {
        if (!SupportsVulkanAccelerationStructures())
        {
            PE_ERROR("AccelerationStructure::BuildTLAS requested without Vulkan ray-tracing support");
            return;
        }

        vk::AccelerationStructureGeometryInstancesDataKHR instancesVk{};
        instancesVk.data.deviceAddress = instanceBuffer->GetDeviceAddress();

        vk::AccelerationStructureGeometryKHR geometry{};
        geometry.geometryType = vk::GeometryTypeKHR::eInstances;
        geometry.geometry.instances = instancesVk;

        // Add eAllowUpdate to enable subsequent UpdateTLAS calls
        PeAccelerationStructureBuildFlags buildFlags = flags | PE_ACCELERATION_STRUCTURE_BUILD_ALLOW_UPDATE;

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        buildInfo.flags = ToVkAccelerationStructureBuildFlags(buildFlags);
        buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        vk::AccelerationStructureBuildSizesInfoKHR sizeInfo{};
        uint32_t count = instanceCount;
        VulkanRhi::Device().getAccelerationStructureBuildSizesKHR(
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
        createInfo.buffer = GetVulkanBuffer(m_buffer);
        createInfo.offset = m_offset;
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        vk::AccelerationStructureKHR apiHandle = VulkanRhi::Device().createAccelerationStructureKHR(createInfo);
        m_apiHandle = detail::ToUintPtr(apiHandle);

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.accelerationStructure = apiHandle;
        m_deviceAddress = VulkanRhi::Device().getAccelerationStructureAddressKHR(&addressInfo);

        buildInfo.dstAccelerationStructure = apiHandle;

        vk::AccelerationStructureBuildRangeInfoKHR buildRange{};
        buildRange.primitiveCount = instanceCount;
        buildRange.primitiveOffset = 0;
        buildRange.firstVertex = 0;
        buildRange.transformOffset = 0;

        const vk::AccelerationStructureBuildRangeInfoKHR *pBuildRanges = &buildRange;
        BuildAccelerationStructures(cmd, 1, &buildInfo, &pBuildRanges);

        // Add barrier for subsequent use
        MemoryBarrierInfo barrier{};
        barrier.srcStageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
        barrier.srcAccessMask = PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
        barrier.dstStageMask = PE_STAGE_RAY_TRACING_SHADER_KHR | PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
        barrier.dstAccessMask = PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR | PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
        cmd->MemoryBarrier(barrier);
    }

    void AccelerationStructure::UpdateTLAS(CommandBuffer *cmd,
                                           uint32_t instanceCount,
                                           Buffer *instanceBuffer,
                                           uint64_t scratchAddress)
    {
        if (!SupportsVulkanAccelerationStructures())
        {
            PE_ERROR("AccelerationStructure::UpdateTLAS requested without Vulkan ray-tracing support");
            return;
        }

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
        vk::AccelerationStructureKHR apiHandle = GetVulkanAccelerationStructure(this);
        buildInfo.srcAccelerationStructure = apiHandle;
        buildInfo.dstAccelerationStructure = apiHandle;
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
        BuildAccelerationStructures(cmd, 1, &buildInfo, &pBuildRanges);

        // Add barrier for subsequent use
        MemoryBarrierInfo barrier{};
        barrier.srcStageMask = PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
        barrier.srcAccessMask = PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
        barrier.dstStageMask = PE_STAGE_RAY_TRACING_SHADER_KHR | PE_STAGE_ACCELERATION_STRUCTURE_BUILD_KHR;
        barrier.dstAccessMask = PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR | PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
        cmd->MemoryBarrier(barrier);
    }

    uint64_t AccelerationStructure::GetDeviceAddress() const
    {
        return m_deviceAddress;
    }
} // namespace pe
