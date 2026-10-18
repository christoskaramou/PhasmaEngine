#pragma once

#include "API/Vulkan/VulkanHeaders.h"
#include "API/RHITypes.h"

namespace pe
{
    class Buffer;
    class Descriptor;
    class DescriptorLayout;

    bool UsesVulkanDescriptorHeap();
    // Sampled-image descriptors that three quarters of the resource heap holds, for bindless arrays.
    uint32_t VulkanHeapTextureBudget();

    constexpr PeBufferUsageFlags VulkanDescriptorHeapUsage = 1ULL << 63;

    struct VulkanHeapBinding
    {
        uint32_t resourceOffset{};
        uint32_t resourceStride{};
        uint32_t indexOffset{};
        uint32_t samplerIndexOffset{};
    };

    struct VulkanHeapLayout
    {
        explicit VulkanHeapLayout(const DescriptorLayout &layout);

        // Heap bytes for a descriptor whose bindless (last) binding holds variableCount entries.
        uint32_t Size(uint32_t variableCount) const;

        std::vector<VulkanHeapBinding> bindings;
        uint32_t fixedSize{};
        uint32_t variableStride{};
        bool supported = true;
    };

    class VulkanDescriptorHeap
    {
    public:
        VulkanDescriptorHeap(bool sampler, const VkPhysicalDeviceDescriptorHeapPropertiesEXT &properties);
        ~VulkanDescriptorHeap();

        uint32_t Allocate(uint32_t size);
        // Returns UINT32_MAX when no free range fits.
        uint32_t TryAllocate(uint32_t size);
        void Free(uint32_t offset, uint32_t size);
        void Flush(uint32_t offset, uint32_t size);
        void Bind(vk::CommandBuffer commandBuffer) const;

        uint8_t *Data(uint32_t offset) const;
        uint64_t Address(uint32_t offset) const;
        uint32_t Alignment() const { return m_alignment; }
        uint32_t UsableSize() const { return static_cast<uint32_t>(m_bindInfo.reservedRangeOffset); }

    private:
        Buffer *m_buffer{};
        VkBindHeapInfoEXT m_bindInfo{};
        std::map<uint32_t, uint32_t> m_freeRanges;
        std::mutex m_mutex;
        uint32_t m_alignment{};
        size_t m_bufferOffset{};
        bool m_sampler{};
    };

    struct VulkanDescriptorHeaps
    {
        explicit VulkanDescriptorHeaps(const VkPhysicalDeviceDescriptorHeapPropertiesEXT &properties);
        void Bind(vk::CommandBuffer commandBuffer) const;

        VulkanDescriptorHeap resources;
        VulkanDescriptorHeap samplers;
    };

    constexpr uint32_t VulkanHeapSetPushOffset = 128;

    struct VulkanHeapSetData
    {
        uint64_t tableAddress{};
        uint32_t resourceOffset{};
        uint32_t padding{};
    };

    // Push data has no bind point, unlike descriptor sets and push constants; each bind point keeps its last values.
    struct VulkanHeapBindState
    {
        std::vector<VulkanHeapSetData> sets;
        std::array<uint8_t, VulkanHeapSetPushOffset> constants{};
        uint32_t constantsSize{};
    };

    void PushVulkanHeapData(vk::CommandBuffer commandBuffer, uint32_t offset, size_t size, const void *data);
    void PushVulkanHeapState(vk::CommandBuffer commandBuffer, const VulkanHeapBindState &state);
    void BindVulkanHeapDescriptors(vk::CommandBuffer commandBuffer, VulkanHeapBindState &state,
                                   uint32_t count, Descriptor *const *descriptors);
} // namespace pe
