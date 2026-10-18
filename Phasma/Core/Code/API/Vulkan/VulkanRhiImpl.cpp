#include "API/Vulkan/VulkanRhiImpl.h"
#include "API/Vulkan/VulkanDescriptorHeap.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"

namespace pe
{
    void VulkanRhiImpl::QueryDescriptorHeapSupport()
    {
        m_descriptorHeapFeatures = {};
        if (RHII.GetCaps().effectiveApiVersion < VK_API_VERSION_1_4 ||
            !RHII.GetCaps().bufferDeviceAddress)
        {
            return;
        }

        if (!RHII.IsDeviceExtensionValid(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME))
            return;

        m_descriptorHeapFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &m_descriptorHeapFeatures;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceFeatures2(m_gpu, &features);

        m_descriptorHeapProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &m_descriptorHeapProperties;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceProperties2(m_gpu, &properties);

        if (m_descriptorHeapProperties.maxPushDataSize < VulkanHeapSetPushOffset + sizeof(VulkanHeapSetData))
            m_descriptorHeapFeatures.descriptorHeap = VK_FALSE;

        m_descriptorHeapFeatures.descriptorHeapCaptureReplay = VK_FALSE;
    }

    void VulkanRhiImpl::CreateDescriptorHeaps()
    {
        if (m_descriptorHeapFeatures.descriptorHeap)
            m_descriptorHeaps = new VulkanDescriptorHeaps(m_descriptorHeapProperties);

        PE_INFO("[Vulkan] Descriptor binding: %s", m_descriptorHeaps ? "descriptor heaps" : "descriptor sets");
    }

    void VulkanRhiImpl::DestroyDescriptorHeaps()
    {
        delete m_descriptorHeaps;
        m_descriptorHeaps = nullptr;
    }

    // Empty: Vulkan setup still lives in RHI::CreateInstance/FindGpu/CreateDevice/...
    bool VulkanRhiImpl::Init(SDL_Window *)
    {
        return true;
    }

    void VulkanRhiImpl::Shutdown() {}

    void VulkanRhiImpl::WaitDeviceIdle()
    {
        VulkanRhi::Device().waitIdle();
    }

    void VulkanRhiImpl::NextFrame() {}
} // namespace pe
