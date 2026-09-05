#include "API/DX12/Dx12SemaphoreImpl.h"

#if defined(PE_WIN32)

#include "API/DX12/Dx12RhiImpl.h"
#include "API/RHI.h"

namespace pe
{
    namespace
    {
        DWORD NsToTimeoutMs(uint64_t timeoutNS)
        {
            if (timeoutNS == UINT64_MAX)
                return INFINITE;
            uint64_t ms = timeoutNS / 1000000ULL;
            if (ms >= static_cast<uint64_t>(INFINITE))
                return INFINITE - 1;
            return static_cast<DWORD>(ms);
        }

        struct ThreadFenceEvent
        {
            HANDLE handle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            ~ThreadFenceEvent()
            {
                if (handle)
                    CloseHandle(handle);
            }
        };
    } // namespace

    bool Dx12WaitFence(ID3D12Fence *fence, uint64_t value, DWORD timeoutMs)
    {
        // One event per waiting thread: a shared auto-reset event lets a lower fence value wake a
        // waiter registered for a higher one. Re-check after every wake so a stale registration
        // (timed-out wait) can only cost a spurious loop iteration, never an early return.
        // GetCompletedValue() reports UINT64_MAX after device removal, which ends the wait.
        thread_local ThreadFenceEvent event;
        PE_ERROR_IF(!event.handle, "Dx12WaitFence: CreateEvent failed");
        while (fence->GetCompletedValue() < value)
        {
            HRESULT hr = fence->SetEventOnCompletion(value, event.handle);
            if (FAILED(hr))
            {
                PE_ERROR("Dx12WaitFence: SetEventOnCompletion failed (0x%08X)", static_cast<unsigned>(hr));
                return false;
            }
            if (WaitForSingleObject(event.handle, timeoutMs) != WAIT_OBJECT_0)
                return fence->GetCompletedValue() >= value;
        }
        return true;
    }

    Dx12SemaphoreImpl::Dx12SemaphoreImpl(Semaphore *owner, bool timeline, const std::string &name)
        : m_owner{owner},
          m_timeline{timeline}
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetDevice(), "Dx12SemaphoreImpl requires an initialized DX12 device");

        // ID3D12Fence is monotonic and naturally maps to Vulkan timeline semaphore semantics.
        // Binary semaphores (Vulkan acquire/submit pair) are also fence-backed; Wait/Signal/
        // GetValue assert on non-timeline use, matching the Vulkan path. The queue-internal
        // increment/wait pattern that drives binary sync lives in Dx12QueueImpl.
        HRESULT hr = rhi->GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
        PE_ERROR_IF(FAILED(hr), "Dx12SemaphoreImpl: CreateFence failed (0x%08X)", static_cast<unsigned>(hr));

        if (!name.empty() && m_fence)
        {
            std::wstring wname(name.begin(), name.end());
            m_fence->SetName(wname.c_str());
        }
    }

    void Dx12SemaphoreImpl::Wait(uint64_t value)
    {
        PE_ERROR_IF(!m_timeline, "Semaphore::Wait() called on non-timeline semaphore!");

        // m_lastCompleted is a monotone-ish cache: a lower store only costs an extra fence read.
        if (value <= m_lastCompleted.load(std::memory_order_relaxed))
            return;

        if (Dx12WaitFence(m_fence.Get(), value, INFINITE))
            m_lastCompleted.store(value, std::memory_order_relaxed);
        else
            PE_ERROR("[Semaphore] Failed to wait for timeline semaphore!");
    }

    bool Dx12SemaphoreImpl::WaitTimeout(uint64_t value, uint64_t timeoutNS)
    {
        PE_ERROR_IF(!m_timeline, "Semaphore::WaitTimeout() called on non-timeline semaphore!");

        if (value <= m_lastCompleted.load(std::memory_order_relaxed))
            return true;

        if (!Dx12WaitFence(m_fence.Get(), value, NsToTimeoutMs(timeoutNS)))
            return false;

        m_lastCompleted.store(value, std::memory_order_relaxed);
        return true;
    }

    void Dx12SemaphoreImpl::Signal(uint64_t value)
    {
        PE_ERROR_IF(!m_timeline, "Semaphore::Signal() called on non-timeline semaphore!");

        HRESULT hr = m_fence->Signal(value);
        PE_ERROR_IF(FAILED(hr), "Dx12SemaphoreImpl::Signal failed (0x%08X)", static_cast<unsigned>(hr));
        m_queueSignalValue = std::max(m_queueSignalValue, value);
    }

    uint64_t Dx12SemaphoreImpl::GetValue()
    {
        if (!m_timeline)
            return 0;

        return m_fence->GetCompletedValue();
    }

    void Dx12SemaphoreImpl::QueueWait(ID3D12CommandQueue *queue, uint64_t value)
    {
        PE_ERROR_IF(!queue, "Dx12SemaphoreImpl::QueueWait: null queue");
        if (value == 0)
            return;

        HRESULT hr = queue->Wait(m_fence.Get(), value);
        PE_ERROR_IF(FAILED(hr), "Dx12SemaphoreImpl::QueueWait failed (0x%08X)", static_cast<unsigned>(hr));
    }

    void Dx12SemaphoreImpl::QueueSignal(ID3D12CommandQueue *queue, uint64_t value)
    {
        PE_ERROR_IF(!queue, "Dx12SemaphoreImpl::QueueSignal: null queue");
        HRESULT hr = queue->Signal(m_fence.Get(), value);
        PE_ERROR_IF(FAILED(hr), "Dx12SemaphoreImpl::QueueSignal failed (0x%08X)", static_cast<unsigned>(hr));
        m_queueSignalValue = std::max(m_queueSignalValue, value);
    }
} // namespace pe

#endif // PE_WIN32
