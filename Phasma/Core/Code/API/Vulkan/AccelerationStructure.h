#pragma once

#include "API/AccelerationStructure.h"
#include "API/Vulkan/VulkanHeaders.h"


namespace pe
{
    struct AccelerationStructureAccess
    {
        static void CreateBuffer(AccelerationStructure *as, size_t size) { as->CreateBuffer(size); }
        static void CreateScratchBuffer(AccelerationStructure *as, size_t size) { as->CreateScratchBuffer(size); }
        static Buffer *GetBuffer(AccelerationStructure *as) { return as->m_buffer; }
        static Buffer *GetScratchBuffer(AccelerationStructure *as) { return as->m_scratchBuffer; }
        static uint64_t GetOffset(AccelerationStructure *as) { return as->m_offset; }
        static bool IsExternalBuffer(AccelerationStructure *as) { return as->m_externalBuffer; }
        static void SetDeviceAddress(AccelerationStructure *as, uint64_t address) { as->m_deviceAddress = address; }
    };

    template <typename VkHandle>
    inline VkHandle VulkanAccelerationStructureFromBackendHandle(PeBackendHandle handle)
    {
        if constexpr (std::is_pointer_v<VkHandle>)
            return reinterpret_cast<VkHandle>(handle);
        else
            return static_cast<VkHandle>(handle);
    }

    inline vk::AccelerationStructureKHR GetVulkanAccelerationStructure(const AccelerationStructure *as)
    {
        if (!as)
            return vk::AccelerationStructureKHR{};

        return vk::AccelerationStructureKHR{VulkanAccelerationStructureFromBackendHandle<VkAccelerationStructureKHR>(as->ApiHandle())};
    }

    vk::AccelerationStructureBuildSizesInfoKHR GetVulkanAccelerationStructureBuildSizes(
        const std::vector<vk::AccelerationStructureGeometryKHR> &geometries,
        const std::vector<uint32_t> &maxPrimitiveCounts,
        vk::AccelerationStructureTypeKHR accelerationStructureType,
        PeAccelerationStructureBuildFlags flags,
        vk::AccelerationStructureBuildTypeKHR type);

    void BuildVulkanBLAS(AccelerationStructure *as,
                         CommandBuffer *cmd,
                         const std::vector<vk::AccelerationStructureGeometryKHR> &geometries,
                         const std::vector<vk::AccelerationStructureBuildRangeInfoKHR> &buildRanges,
                         const std::vector<uint32_t> &maxPrimitiveCounts,
                         PeAccelerationStructureBuildFlags flags,
                         vk::DeviceAddress scratchAddress = 0);
    void BuildVulkanTLAS(AccelerationStructure *as,
                         CommandBuffer *cmd,
                         uint32_t instanceCount,
                         Buffer *instanceBuffer,
                         PeAccelerationStructureBuildFlags flags,
                         vk::DeviceAddress scratchAddress = 0);
    void UpdateVulkanTLAS(AccelerationStructure *as,
                          CommandBuffer *cmd,
                          uint32_t instanceCount,
                          Buffer *instanceBuffer,
                          vk::DeviceAddress scratchAddress = 0);
    void DestroyVulkanAccelerationStructure(AccelerationStructure *as);

    // Vulkan-private RT-build entry. Called by AccelerationStructure::Build*; not part
    // of the neutral CommandBuffer surface (DXR, when added, will get its own entry).
    void BuildAccelerationStructures(CommandBuffer *cmd,
                                     uint32_t infoCount,
                                     const vk::AccelerationStructureBuildGeometryInfoKHR *pInfos,
                                     const vk::AccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos);
} // namespace pe
