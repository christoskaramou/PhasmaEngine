#pragma once

#include "API/Queue.h"

namespace pe
{
    class CommandBuffer;
    class Swapchain;

    struct Queue::Impl : public NoCopy
    {
        virtual ~Impl() = default;

        virtual void Submit(uint32_t commandBuffersCount,
                            CommandBuffer *const *commandBuffers,
                            Semaphore *wait,
                            Semaphore *signal,
                            Semaphore *submissionsSemaphore,
                            uint64_t submissionValue) = 0;
        virtual void Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait) = 0;
        virtual void WaitIdle() = 0;
    };

    Queue::Impl *CreateQueueImpl(Queue *owner, uint32_t familyId, const std::string &name);
} // namespace pe
