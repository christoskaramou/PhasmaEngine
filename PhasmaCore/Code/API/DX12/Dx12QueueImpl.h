#pragma once

#include "API/Queue_Internal.h"

#if defined(PE_WIN32)

#include <d3d12.h>

namespace pe
{
    struct Dx12QueueImpl final : public Queue::Impl
    {
        Dx12QueueImpl(Queue *owner, uint32_t familyId, const std::string &name);
        ~Dx12QueueImpl() override = default;

        static Dx12QueueImpl *From(Queue *q) { return static_cast<Dx12QueueImpl *>(q->m_impl); }
        static const Dx12QueueImpl *From(const Queue *q) { return static_cast<const Dx12QueueImpl *>(q->m_impl); }

        void Submit(uint32_t commandBuffersCount,
                    CommandBuffer *const *commandBuffers,
                    Semaphore *wait,
                    Semaphore *signal,
                    Semaphore *submissionsSemaphore,
                    uint64_t submissionValue) override;
        void Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait) override;
        void WaitIdle() override;

        Queue *m_owner{};
        ID3D12CommandQueue *m_queue{};
    };
} // namespace pe

#endif // PE_WIN32
