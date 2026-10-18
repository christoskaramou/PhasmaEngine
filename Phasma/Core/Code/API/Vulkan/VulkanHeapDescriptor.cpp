#include "API/Vulkan/VulkanDescriptorImpl.h"
#include "API/AccelerationStructure.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanRHITypeUtils.h"

namespace pe
{
    namespace
    {
        bool IsNullResource(const DescriptorUpdateInfo &update, uint32_t element)
        {
            if (!update.views.empty())
                return !update.views[element];

            if (!update.buffers.empty())
                return !update.buffers[element];

            return !update.accelerationStructures.empty() && !update.accelerationStructures[element];
        }

        VkDeviceAddressRangeEXT HeapBufferAddress(const DescriptorUpdateInfo &update, uint32_t element)
        {
            Buffer *buffer = update.buffers[element];
            const uint64_t offset = update.offsets.empty() ? 0 : update.offsets[element];
            PE_ERROR_IF(offset > buffer->Size(), "Descriptor heap buffer offset exceeds the buffer size");

            const uint64_t requestedSize = update.ranges.empty() ? 0 : update.ranges[element];
            const bool wholeBuffer = requestedSize == 0 || requestedSize == UINT64_MAX;
            const uint64_t size = wholeBuffer ? buffer->Size() - offset : requestedSize;
            PE_ERROR_IF(size > buffer->Size() - offset, "Descriptor heap buffer range exceeds the buffer size");

            return {VulkanBufferImpl::From(buffer)->GetDeviceAddress() + offset, size};
        }

        VkDescriptorType HeapResourceType(PeBindingType type)
        {
            switch (type)
            {
            case PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case PE_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case PE_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            default:
                return static_cast<VkDescriptorType>(ToVkDescriptorType(type));
            }
        }
    } // namespace

    void VulkanDescriptorImpl::InitializeHeap()
    {
        const auto &layout = *VulkanDescriptorLayoutImpl::From(m_owner->GetLayout())->m_heapLayout;
        auto &heap = VulkanRhi::Impl()->m_descriptorHeaps->resources;
        m_heapSize = layout.Size(m_owner->GetVariableCount());
        // Growing a bindless array may not fit; the caller keeps the previous descriptor instead of failing.
        m_heapOffset = heap.TryAllocate(m_heapSize);
        if (m_heapOffset == UINT32_MAX)
        {
            throw std::runtime_error("Vulkan resource descriptor heap has no free range of " +
                                     std::to_string(m_heapSize) +
                                     " bytes");
        }

        std::memset(heap.Data(m_heapOffset), 0, m_heapSize);

        for (size_t i = 0; i < layout.bindings.size(); ++i)
        {
            const auto &info = m_owner->GetBindingInfos()[i];
            // Only combined bindings read resource indices from a table; the memset points sampler tables at slot 0.
            if (info.type != PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                continue;

            const auto &binding = layout.bindings[i];
            auto *indices = reinterpret_cast<uint32_t *>(heap.Data(m_heapOffset + binding.indexOffset));
            const uint32_t capacity = m_owner->GetBindingCapacity(i);
            for (uint32_t element = 0; element < capacity; ++element)
                indices[element] = m_heapOffset + binding.resourceOffset + element * binding.resourceStride;
        }

        heap.Flush(m_heapOffset, m_heapSize);
    }

    void VulkanDescriptorImpl::WriteHeapResource(uint32_t bindingIndex, uint32_t element,
                                                 const DescriptorUpdateInfo &update)
    {
        const auto &layout = *VulkanDescriptorLayoutImpl::From(m_owner->GetLayout())->m_heapLayout;
        const auto &binding = layout.bindings[bindingIndex];
        const auto &info = m_owner->GetBindingInfos()[bindingIndex];
        auto &heap = VulkanRhi::Impl()->m_descriptorHeaps->resources;
        const uint32_t offset = m_heapOffset + binding.resourceOffset + element * binding.resourceStride;

        // The descriptor-set path writes VK_NULL_HANDLE for a missing resource; leave an empty descriptor to match.
        if (IsNullResource(update, element))
        {
            std::memset(heap.Data(offset), 0, binding.resourceStride);
            return;
        }

        VkResourceDescriptorInfoEXT resource{VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
        resource.type = HeapResourceType(info.type);
        VkImageDescriptorInfoEXT image{VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT};
        VkDeviceAddressRangeEXT address{};

        if (!update.views.empty())
        {
            const auto *view = VulkanImageViewImpl::From(update.views[element]);
            image.pView = reinterpret_cast<const VkImageViewCreateInfo *>(&view->m_createInfo);
            image.layout = static_cast<VkImageLayout>(ToVkImageLayout(info.imageLayout));
            resource.data.pImage = &image;
        }
        else if (!update.buffers.empty())
        {
            address = HeapBufferAddress(update, element);
            resource.data.pAddressRange = &address;
        }
        else if (!update.accelerationStructures.empty())
        {
            address.address = update.accelerationStructures[element]->GetDeviceAddress();
            resource.data.pAddressRange = &address;
        }
        else
        {
            return;
        }

        VkHostAddressRangeEXT destination{heap.Data(offset), binding.resourceStride};
        PE_CHECK(VULKAN_HPP_DEFAULT_DISPATCHER.vkWriteResourceDescriptorsEXT(
            VulkanRhi::Device(), 1, &resource, &destination));
    }

    void VulkanDescriptorImpl::UpdateHeap(const std::vector<DescriptorUpdateInfo> &updateInfos)
    {
        const auto &layout = *VulkanDescriptorLayoutImpl::From(m_owner->GetLayout())->m_heapLayout;
        auto &heap = VulkanRhi::Impl()->m_descriptorHeaps->resources;

        for (uint32_t i = 0; i < updateInfos.size(); ++i)
        {
            const auto &update = updateInfos[i];
            const auto &binding = layout.bindings[i];
            const auto &info = m_owner->GetBindingInfos()[i];
            const uint32_t capacity = m_owner->GetBindingCapacity(i);
            const uint32_t count = std::max({update.views.size(),
                                             update.buffers.size(),
                                             update.accelerationStructures.size()});
            PE_ERROR_IF(count > capacity || update.samplers.size() > capacity,
                        "Descriptor heap update exceeds the binding size");

            for (uint32_t element = 0; element < count; ++element)
                WriteHeapResource(i, element, update);

            const auto type = info.type;
            if (type != PE_DESCRIPTOR_TYPE_SAMPLER && type != PE_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                continue;

            auto *indices = reinterpret_cast<uint32_t *>(heap.Data(m_heapOffset + binding.samplerIndexOffset));
            // A null sampler (VK_NULL_HANDLE on the descriptor-set path) falls back to slot 0, like an unwritten entry.
            for (uint32_t element = 0; element < update.samplers.size(); ++element)
            {
                Sampler *sampler = update.samplers[element];
                indices[element] = sampler ? VulkanSamplerImpl::From(sampler)->m_heapOffset : 0;
            }
        }

        heap.Flush(m_heapOffset, m_heapSize);
    }
} // namespace pe
