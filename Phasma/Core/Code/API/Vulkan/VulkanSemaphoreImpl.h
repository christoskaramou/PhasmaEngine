#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/Semaphore_Internal.h"

namespace pe
{
    struct VulkanSemaphoreImpl final : public Semaphore::Impl
    {
        VulkanSemaphoreImpl(Semaphore *owner, bool timeline, const std::string &name);
        ~VulkanSemaphoreImpl() override;

        static VulkanSemaphoreImpl *From(Semaphore *s) { return static_cast<VulkanSemaphoreImpl *>(s->m_impl); }
        static const VulkanSemaphoreImpl *From(const Semaphore *s) { return static_cast<const VulkanSemaphoreImpl *>(s->m_impl); }
        static VulkanSemaphoreImpl *TryFrom(Semaphore *s) { return s && s->m_impl ? From(s) : nullptr; }
        static const VulkanSemaphoreImpl *TryFrom(const Semaphore *s) { return s && s->m_impl ? From(s) : nullptr; }

        void Wait(uint64_t value) override;
        bool WaitTimeout(uint64_t value, uint64_t timeoutNS) override;
        void Signal(uint64_t value) override;
        uint64_t GetValue() override;

        Semaphore *m_owner{};
        vk::Semaphore m_apiHandle{};
        bool m_timeline{false};
        uint64_t m_lastCompleted{0};
    };

    inline vk::Semaphore GetVulkanSemaphore(Semaphore *s)
    {
        VulkanSemaphoreImpl *impl = VulkanSemaphoreImpl::TryFrom(s);
        return impl ? impl->m_apiHandle : vk::Semaphore{};
    }

    inline vk::Semaphore GetVulkanSemaphore(const Semaphore *s)
    {
        const VulkanSemaphoreImpl *impl = VulkanSemaphoreImpl::TryFrom(s);
        return impl ? impl->m_apiHandle : vk::Semaphore{};
    }
} // namespace pe
