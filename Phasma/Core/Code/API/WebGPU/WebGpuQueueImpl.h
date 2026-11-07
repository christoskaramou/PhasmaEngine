#pragma once

#include "API/CommandPool_Internal.h"
#include "API/Queue_Internal.h"
#include "API/Semaphore_Internal.h"

namespace pe
{
    // The one WebGPU queue orders all work and tracks resource use, so semaphores and waits have nothing to wait for
    // (spec decision 5). Reusing staging space right after a wait is safe: a later writeBuffer is queued after the
    // submit that read the old contents.
    struct WebGpuQueueImpl final : public Queue::Impl
    {
        void Submit(uint32_t commandBuffersCount,
                    CommandBuffer *const *commandBuffers,
                    Semaphore *wait,
                    Semaphore *signal,
                    Semaphore *submissionsSemaphore,
                    uint64_t submissionValue) override;
        void Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait) override;
        void WaitIdle() override {}
    };

    struct WebGpuCommandPoolImpl final : public CommandPool::Impl
    {
        void Reset() override {}
    };

    struct WebGpuSemaphoreImpl final : public Semaphore::Impl
    {
        void Wait(uint64_t) override {}
        bool WaitTimeout(uint64_t, uint64_t) override { return true; }
        void Signal(uint64_t value) override { m_value = std::max(m_value, value); }
        uint64_t GetValue() override { return m_value; }

        uint64_t m_value = 0;
    };
} // namespace pe
