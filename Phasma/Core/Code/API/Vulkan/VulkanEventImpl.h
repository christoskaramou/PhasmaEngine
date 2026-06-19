#pragma once

#include "API/Event.h"
#include "API/Vulkan/VulkanHeaders.h"

namespace pe
{
    struct Event::Impl : public NoCopy
    {
        virtual ~Impl() = default;
    };

    struct VulkanEventImpl final : public Event::Impl
    {
        explicit VulkanEventImpl(const std::string &name);
        ~VulkanEventImpl() override;

        static VulkanEventImpl *From(Event *event) { return static_cast<VulkanEventImpl *>(event->m_impl); }
        static const VulkanEventImpl *From(const Event *event) { return static_cast<const VulkanEventImpl *>(event->m_impl); }
        static VulkanEventImpl *TryFrom(Event *event) { return event && event->m_impl ? From(event) : nullptr; }
        static const VulkanEventImpl *TryFrom(const Event *event) { return event && event->m_impl ? From(event) : nullptr; }

        vk::Event m_apiHandle{};
    };

    inline vk::Event GetVulkanEvent(Event *event)
    {
        VulkanEventImpl *impl = VulkanEventImpl::TryFrom(event);
        return impl ? impl->m_apiHandle : vk::Event{};
    }

    inline vk::Event GetVulkanEvent(const Event *event)
    {
        const VulkanEventImpl *impl = VulkanEventImpl::TryFrom(event);
        return impl ? impl->m_apiHandle : vk::Event{};
    }
} // namespace pe
