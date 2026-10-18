#include "API/Vulkan/VulkanDescriptorHeap.h"
#include "API/Buffer.h"
#include "API/Descriptor.h"
#include "API/Vulkan/RHI_Vulkan.h"

namespace pe
{
    bool UsesVulkanDescriptorHeap()
    {
        return VulkanRhi::Impl()->m_descriptorHeapFeatures.descriptorHeap != VK_FALSE;
    }

    namespace
    {
        uint32_t AlignHeapSize(uint32_t size, uint32_t alignment)
        {
            return (size + alignment - 1) / alignment * alignment;
        }

        bool IsCombinedBinding(PeBindingType type)
        {
            return type == PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        }

        uint32_t ResourceStride(PeBindingType type, const VkPhysicalDeviceDescriptorHeapPropertiesEXT &properties)
        {
            switch (type)
            {
            case PE_DESCRIPTOR_TYPE_SAMPLER:
                return 0;
            case PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            case PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case PE_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                return AlignHeapSize(properties.imageDescriptorSize, properties.imageDescriptorAlignment);
            case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case PE_DESCRIPTOR_TYPE_STRUCTURED_BUFFER:
            case PE_DESCRIPTOR_TYPE_BYTE_ADDRESS_BUFFER:
            case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            case PE_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE:
                return AlignHeapSize(properties.bufferDescriptorSize, properties.bufferDescriptorAlignment);
            default:
                return UINT32_MAX;
            }
        }
    } // namespace

    uint32_t VulkanHeapTextureBudget()
    {
        const auto *rhi = VulkanRhi::Impl();
        const uint32_t stride = ResourceStride(PE_DESCRIPTOR_TYPE_SAMPLED_IMAGE, rhi->m_descriptorHeapProperties);
        return rhi->m_descriptorHeaps->resources.UsableSize() / stride / 4 * 3;
    }

    VulkanHeapLayout::VulkanHeapLayout(const DescriptorLayout &layout)
    {
        const auto &properties = VulkanRhi::Impl()->m_descriptorHeapProperties;
        const uint32_t alignment = std::max(properties.imageDescriptorAlignment, properties.bufferDescriptorAlignment);

        uint32_t size = 0;
        for (const auto &binding : layout.GetBindingInfos())
        {
            const uint32_t stride = ResourceStride(binding.type, properties);
            if (stride == UINT32_MAX || layout.IsPushDescriptor())
            {
                supported = false;
                return;
            }

            // Only sampler and combined bindings read indices from tables; the others use push indices.
            VulkanHeapBinding heapBinding{};
            if (binding.type == PE_DESCRIPTOR_TYPE_SAMPLER || IsCombinedBinding(binding.type))
            {
                heapBinding.indexOffset = size;
                size += binding.count * sizeof(uint32_t);
                heapBinding.samplerIndexOffset = IsCombinedBinding(binding.type) ? size : heapBinding.indexOffset;
                if (IsCombinedBinding(binding.type))
                    size += binding.count * sizeof(uint32_t);
            }

            size = AlignHeapSize(size, alignment);
            heapBinding.resourceOffset = size;
            heapBinding.resourceStride = stride;
            // A bindless binding is last, so its resources can follow the fixed part at whatever capacity it has.
            if (binding.bindless)
                variableStride = stride;
            else
                size += binding.count * stride;

            bindings.push_back(heapBinding);
        }

        fixedSize = size;
    }

    uint32_t VulkanHeapLayout::Size(uint32_t variableCount) const
    {
        const uint64_t size = std::max<uint64_t>(1, fixedSize + uint64_t{variableCount} * variableStride);
        const uint32_t alignment = VulkanRhi::Impl()->m_descriptorHeaps->resources.Alignment();
        if (size > UINT32_MAX - alignment)
            throw std::runtime_error("Vulkan descriptor heap range exceeds 4 GiB");

        return AlignHeapSize(static_cast<uint32_t>(size), alignment);
    }

    VulkanDescriptorHeap::VulkanDescriptorHeap(bool sampler,
                                               const VkPhysicalDeviceDescriptorHeapPropertiesEXT &properties)
        : m_sampler{sampler}
    {
        const uint32_t heapAlignment = sampler ? properties.samplerHeapAlignment : properties.resourceHeapAlignment;
        const uint32_t resourceAlignment = std::max(properties.imageDescriptorAlignment,
                                                    properties.bufferDescriptorAlignment);
        m_alignment = sampler ? properties.samplerDescriptorAlignment : resourceAlignment;
        const uint32_t reservation = sampler
                                         ? properties.minSamplerHeapReservedRange
                                         : properties.minResourceHeapReservedRange;
        const uint64_t deviceMaximum = sampler ? properties.maxSamplerHeapSize : properties.maxResourceHeapSize;
        const uint32_t maximum = std::min<uint64_t>(UINT32_MAX, deviceMaximum);
        // ponytail: stable heaps capped at 32 MiB / 128 KiB; add growth only if scene allocations exhaust them.
        const uint32_t capacity = std::min(maximum, sampler ? 128u * 1024u : 32u * 1024u * 1024u);
        PE_ERROR_IF(capacity <= reservation, "Vulkan descriptor heap has no application storage");
        const uint32_t usableSize = (capacity - reservation) / m_alignment * m_alignment;

        BufferDesc desc{};
        desc.size = capacity + heapAlignment;
        desc.usage = PE_BUFFER_USAGE_SHADER_DEVICE_ADDRESS | VulkanDescriptorHeapUsage;
        desc.memoryUsage = PE_MEMORY_USAGE_CPU_TO_GPU_PERSISTENT_DEVICE;
        desc.name = sampler ? "Vulkan sampler heap" : "Vulkan resource heap";
        m_buffer = Buffer::Create(desc);
        m_buffer->Map();

        const uint64_t address = m_buffer->GetDeviceAddress();
        const uint64_t alignedAddress = (address + heapAlignment - 1) / heapAlignment * heapAlignment;
        m_bufferOffset = alignedAddress - address;
        m_bindInfo.sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT;
        m_bindInfo.heapRange = {alignedAddress, capacity};
        m_bindInfo.reservedRangeOffset = usableSize;
        m_bindInfo.reservedRangeSize = capacity - usableSize;
        m_freeRanges.emplace(0, usableSize);
    }

    VulkanDescriptorHeap::~VulkanDescriptorHeap()
    {
        Buffer::Destroy(m_buffer);
    }

    uint32_t VulkanDescriptorHeap::Allocate(uint32_t size)
    {
        const uint32_t offset = TryAllocate(size);
        PE_ERROR_IF(offset == UINT32_MAX,
                    "Vulkan %s descriptor heap exhausted (requested %u bytes)",
                    m_sampler ? "sampler" : "resource",
                    size);
        return offset;
    }

    uint32_t VulkanDescriptorHeap::TryAllocate(uint32_t size)
    {
        size = AlignHeapSize(size, m_alignment);
        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto it = m_freeRanges.begin(); it != m_freeRanges.end(); ++it)
        {
            if (it->second < size)
                continue;

            const uint32_t offset = it->first;
            const uint32_t remaining = it->second - size;
            m_freeRanges.erase(it);
            if (remaining)
                m_freeRanges.emplace(offset + size, remaining);

            return offset;
        }

        return UINT32_MAX;
    }

    void VulkanDescriptorHeap::Free(uint32_t offset, uint32_t size)
    {
        size = AlignHeapSize(size, m_alignment);
        std::lock_guard<std::mutex> lock(m_mutex);
        auto next = m_freeRanges.lower_bound(offset);

        if (next != m_freeRanges.begin())
        {
            auto previous = std::prev(next);
            if (previous->first + previous->second == offset)
            {
                offset = previous->first;
                size += previous->second;
                m_freeRanges.erase(previous);
            }
        }

        if (next != m_freeRanges.end() && offset + size == next->first)
        {
            size += next->second;
            m_freeRanges.erase(next);
        }

        m_freeRanges.emplace(offset, size);
    }

    uint8_t *VulkanDescriptorHeap::Data(uint32_t offset) const
    {
        return static_cast<uint8_t *>(m_buffer->Data()) + m_bufferOffset + offset;
    }

    uint64_t VulkanDescriptorHeap::Address(uint32_t offset) const
    {
        return m_bindInfo.heapRange.address + offset;
    }

    void VulkanDescriptorHeap::Flush(uint32_t offset, uint32_t size)
    {
        m_buffer->Flush(size, m_bufferOffset + offset);
    }

    void VulkanDescriptorHeap::Bind(vk::CommandBuffer commandBuffer) const
    {
        auto &dispatcher = VULKAN_HPP_DEFAULT_DISPATCHER;
        if (m_sampler)
            dispatcher.vkCmdBindSamplerHeapEXT(commandBuffer, &m_bindInfo);
        else
            dispatcher.vkCmdBindResourceHeapEXT(commandBuffer, &m_bindInfo);
    }

    VulkanDescriptorHeaps::VulkanDescriptorHeaps(const VkPhysicalDeviceDescriptorHeapPropertiesEXT &properties)
        : resources{false, properties}, samplers{true, properties}
    {
    }

    void VulkanDescriptorHeaps::Bind(vk::CommandBuffer commandBuffer) const
    {
        resources.Bind(commandBuffer);
        samplers.Bind(commandBuffer);
    }

    void PushVulkanHeapData(vk::CommandBuffer commandBuffer, uint32_t offset, size_t size, const void *data)
    {
        VkPushDataInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT;
        info.offset = offset;
        info.data = {data, size};
        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPushDataEXT(commandBuffer, &info);
    }

    void PushVulkanHeapState(vk::CommandBuffer commandBuffer, const VulkanHeapBindState &state)
    {
        if (state.constantsSize)
            PushVulkanHeapData(commandBuffer, 0, state.constantsSize, state.constants.data());

        if (!state.sets.empty())
        {
            const size_t size = state.sets.size() * sizeof(VulkanHeapSetData);
            PushVulkanHeapData(commandBuffer, VulkanHeapSetPushOffset, size, state.sets.data());
        }
    }

    void BindVulkanHeapDescriptors(vk::CommandBuffer commandBuffer, VulkanHeapBindState &state,
                                   uint32_t count, Descriptor *const *descriptors)
    {
        const auto maxPushDataSize = VulkanRhi::Impl()->m_descriptorHeapProperties.maxPushDataSize;
        PE_ERROR_IF(VulkanHeapSetPushOffset + count * sizeof(VulkanHeapSetData) > maxPushDataSize,
                    "Descriptor sets exceed the Vulkan descriptor heap push data limit");

        // Like vkCmdBindDescriptorSets, sets that are not rebound here keep their previous values.
        if (state.sets.size() < count)
            state.sets.resize(count);

        for (uint32_t set = 0; set < count; ++set)
        {
            if (!descriptors[set])
                continue;

            const auto *descriptor = VulkanDescriptorImpl::From(descriptors[set]);
            PE_ERROR_IF(descriptor->m_heapOffset == UINT32_MAX, "Descriptor cannot be bound to a heap pipeline");
            VulkanHeapSetData &data = state.sets[set];
            data.tableAddress = VulkanRhi::Impl()->m_descriptorHeaps->resources.Address(descriptor->m_heapOffset);
            data.resourceOffset = descriptor->m_heapOffset;
            const uint32_t offset = VulkanHeapSetPushOffset + set * sizeof(VulkanHeapSetData);
            PushVulkanHeapData(commandBuffer, offset, sizeof(data), &data);
        }
    }
} // namespace pe
