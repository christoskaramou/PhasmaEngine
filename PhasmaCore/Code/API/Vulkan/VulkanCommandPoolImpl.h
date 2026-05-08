#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/CommandPool_Internal.h"

namespace pe
{
    struct VulkanCommandPoolImpl final : public CommandPool::Impl
    {
        VulkanCommandPoolImpl(CommandPool *owner, Queue *queue, PeCommandPoolCreateFlags flags, const std::string &name);
        ~VulkanCommandPoolImpl() override;

        static VulkanCommandPoolImpl *From(CommandPool *cp) { return static_cast<VulkanCommandPoolImpl *>(cp->m_impl); }
        static const VulkanCommandPoolImpl *From(const CommandPool *cp) { return static_cast<const VulkanCommandPoolImpl *>(cp->m_impl); }
        static VulkanCommandPoolImpl *TryFrom(CommandPool *cp) { return cp && cp->m_impl ? From(cp) : nullptr; }
        static const VulkanCommandPoolImpl *TryFrom(const CommandPool *cp) { return cp && cp->m_impl ? From(cp) : nullptr; }

        void Reset() override;

        CommandPool *m_owner{};
        vk::CommandPool m_apiHandle{};
    };

    inline vk::CommandPool GetVulkanCommandPool(CommandPool *cp)
    {
        VulkanCommandPoolImpl *impl = VulkanCommandPoolImpl::TryFrom(cp);
        return impl ? impl->m_apiHandle : vk::CommandPool{};
    }

    inline vk::CommandPool GetVulkanCommandPool(const CommandPool *cp)
    {
        const VulkanCommandPoolImpl *impl = VulkanCommandPoolImpl::TryFrom(cp);
        return impl ? impl->m_apiHandle : vk::CommandPool{};
    }
} // namespace pe
