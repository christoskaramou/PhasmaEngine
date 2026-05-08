#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/Queue_Internal.h"

namespace pe
{
    struct VulkanQueueImpl final : public Queue::Impl
    {
        VulkanQueueImpl(Queue *owner, uint32_t familyId, const std::string &name);
        ~VulkanQueueImpl() override;

        static VulkanQueueImpl *From(Queue *q) { return static_cast<VulkanQueueImpl *>(q->m_impl); }
        static const VulkanQueueImpl *From(const Queue *q) { return static_cast<const VulkanQueueImpl *>(q->m_impl); }
        static VulkanQueueImpl *TryFrom(Queue *q) { return q && q->m_impl ? From(q) : nullptr; }
        static const VulkanQueueImpl *TryFrom(const Queue *q) { return q && q->m_impl ? From(q) : nullptr; }

        void Submit(uint32_t commandBuffersCount,
                    CommandBuffer *const *commandBuffers,
                    Semaphore *wait,
                    Semaphore *signal,
                    Semaphore *submissionsSemaphore,
                    uint64_t submissionValue) override;
        void Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait) override;
        void WaitIdle() override;

        Queue *m_owner{};
        vk::Queue m_apiHandle{};
        std::string m_name;
    };

    inline vk::Queue GetVulkanQueue(Queue *q)
    {
        VulkanQueueImpl *impl = VulkanQueueImpl::TryFrom(q);
        return impl ? impl->m_apiHandle : vk::Queue{};
    }

    inline vk::Queue GetVulkanQueue(const Queue *q)
    {
        const VulkanQueueImpl *impl = VulkanQueueImpl::TryFrom(q);
        return impl ? impl->m_apiHandle : vk::Queue{};
    }
} // namespace pe
