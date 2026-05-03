#include "API/Vulkan/VulkanCommandPoolImpl.h"
#include "API/Debug.h"
#include "API/Queue.h"
#include "API/Vulkan/RHI_Vulkan.h"

namespace pe
{
    VulkanCommandPoolImpl::VulkanCommandPoolImpl(CommandPool *owner, Queue *queue, vk::CommandPoolCreateFlags flags, const std::string &name)
        : m_owner{owner}
    {
        vk::CommandPoolCreateInfo cpci{};
        cpci.queueFamilyIndex = queue->GetFamilyId();
        cpci.flags = flags;

        m_apiHandle = VulkanRhi::Device().createCommandPool(cpci);
        m_owner->m_apiHandle = m_apiHandle;
        Debug::SetObjectName(m_apiHandle, name);
    }

    VulkanCommandPoolImpl::~VulkanCommandPoolImpl()
    {
        if (m_apiHandle)
            VulkanRhi::Device().destroyCommandPool(m_apiHandle);
    }

    void VulkanCommandPoolImpl::Reset()
    {
        VulkanRhi::Device().resetCommandPool(m_apiHandle);
    }
} // namespace pe
