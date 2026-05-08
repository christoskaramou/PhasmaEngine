#include "API/Vulkan/VulkanCommandPoolImpl.h"
#include "API/Debug.h"
#include "API/Queue.h"
#include "API/Vulkan/RHI_Vulkan.h"

namespace pe
{
    namespace
    {
        vk::CommandPoolCreateFlags ToVkCommandPoolCreateFlags(PeCommandPoolCreateFlags flags)
        {
            vk::CommandPoolCreateFlags out{};
            if (flags & PE_COMMAND_POOL_CREATE_TRANSIENT)
                out |= vk::CommandPoolCreateFlagBits::eTransient;
            if (flags & PE_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER)
                out |= vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
            return out;
        }
    } // namespace

    VulkanCommandPoolImpl::VulkanCommandPoolImpl(CommandPool *owner, Queue *queue, PeCommandPoolCreateFlags flags, const std::string &name)
        : m_owner{owner}
    {
        vk::CommandPoolCreateInfo cpci{};
        cpci.queueFamilyIndex = queue->GetFamilyId();
        cpci.flags = ToVkCommandPoolCreateFlags(flags);

        m_apiHandle = VulkanRhi::Device().createCommandPool(cpci);
        m_owner->m_apiHandle = detail::ToUintPtr(m_apiHandle);
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
