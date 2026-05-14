#include "API/Vulkan/AccelerationStructure.h"

#include "API/Buffer.h"
#include "API/Command.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"

#include <algorithm>

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

    void DestroyVulkanAccelerationStructure(AccelerationStructure *as)
    {
        if (!as || !as->ApiHandle())
            return;
        VulkanRhi::Device().destroyAccelerationStructureKHR(GetVulkanAccelerationStructure(as));
        as->ApiHandle() = {};
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
            AccelerationStructureAccess::CreateScratchBuffer(
                as, std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize));
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

    void BuildVulkanTLAS(AccelerationStructure *as,
                         CommandBuffer *cmd,
                         uint32_t instanceCount,
                         Buffer *instanceBuffer,
                         PeAccelerationStructureBuildFlags flags,
                         vk::DeviceAddress scratchAddress)
    {
        if (!SupportsVulkanAccelerationStructures())
        {
            PE_ERROR("Vulkan TLAS build requested without Vulkan ray-tracing support");
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

        if (!AccelerationStructureAccess::IsExternalBuffer(as))
            AccelerationStructureAccess::CreateBuffer(as, sizeInfo.accelerationStructureSize);

        if (!scratchAddress)
        {
            AccelerationStructureAccess::CreateScratchBuffer(
                as, std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize));
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
        createInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
        vk::AccelerationStructureKHR apiHandle = VulkanRhi::Device().createAccelerationStructureKHR(createInfo);
        as->ApiHandle() = detail::ToUintPtr(apiHandle);

        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.accelerationStructure = apiHandle;
        AccelerationStructureAccess::SetDeviceAddress(
            as, VulkanRhi::Device().getAccelerationStructureAddressKHR(&addressInfo));

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

    void UpdateVulkanTLAS(AccelerationStructure *as,
                          CommandBuffer *cmd,
                          uint32_t instanceCount,
                          Buffer *instanceBuffer,
                          vk::DeviceAddress scratchAddress)
    {
        if (!SupportsVulkanAccelerationStructures())
        {
            PE_ERROR("Vulkan TLAS update requested without Vulkan ray-tracing support");
            return;
        }

        if (!as->ApiHandle())
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
        vk::AccelerationStructureKHR apiHandle = GetVulkanAccelerationStructure(as);
        buildInfo.srcAccelerationStructure = apiHandle;
        buildInfo.dstAccelerationStructure = apiHandle;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &geometry;

        if (scratchAddress)
            buildInfo.scratchData.deviceAddress = scratchAddress;
        else if (AccelerationStructureAccess::GetScratchBuffer(as))
            buildInfo.scratchData.deviceAddress = AccelerationStructureAccess::GetScratchBuffer(as)->GetDeviceAddress();

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
} // namespace pe
