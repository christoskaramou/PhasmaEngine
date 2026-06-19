#pragma once

#include "API/Semaphore_Internal.h"

#if defined(PE_WIN32)

#include <d3d12.h>
#include <wrl/client.h>

namespace pe
{
    struct Dx12SemaphoreImpl final : public Semaphore::Impl
    {
        Dx12SemaphoreImpl(Semaphore *owner, bool timeline, const std::string &name);
        ~Dx12SemaphoreImpl() override;

        static Dx12SemaphoreImpl *From(Semaphore *s) { return static_cast<Dx12SemaphoreImpl *>(s->m_impl); }
        static const Dx12SemaphoreImpl *From(const Semaphore *s) { return static_cast<const Dx12SemaphoreImpl *>(s->m_impl); }
        static Dx12SemaphoreImpl *TryFrom(Semaphore *s) { return s && s->m_impl ? From(s) : nullptr; }
        static const Dx12SemaphoreImpl *TryFrom(const Semaphore *s) { return s && s->m_impl ? From(s) : nullptr; }

        void Wait(uint64_t value) override;
        bool WaitTimeout(uint64_t value, uint64_t timeoutNS) override;
        void Signal(uint64_t value) override;
        uint64_t GetValue() override;

        ID3D12Fence *GetFence() const { return m_fence.Get(); }
        bool IsTimeline() const { return m_timeline; }
        uint64_t CurrentQueueSignalValue() const { return m_queueSignalValue; }
        uint64_t NextQueueSignalValue() { return ++m_queueSignalValue; }
        void QueueWait(ID3D12CommandQueue *queue, uint64_t value);
        void QueueSignal(ID3D12CommandQueue *queue, uint64_t value);

        Semaphore *m_owner{};
        Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
        HANDLE m_event{nullptr};
        bool m_timeline{false};
        uint64_t m_lastCompleted{0};
        uint64_t m_queueSignalValue{0};
    };

    inline ID3D12Fence *GetDx12Fence(Semaphore *s)
    {
        Dx12SemaphoreImpl *impl = Dx12SemaphoreImpl::TryFrom(s);
        return impl ? impl->GetFence() : nullptr;
    }

    inline ID3D12Fence *GetDx12Fence(const Semaphore *s)
    {
        const Dx12SemaphoreImpl *impl = Dx12SemaphoreImpl::TryFrom(s);
        return impl ? impl->GetFence() : nullptr;
    }
} // namespace pe

#endif // PE_WIN32
