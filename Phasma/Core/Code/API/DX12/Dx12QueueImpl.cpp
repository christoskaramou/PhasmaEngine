#include "API/DX12/Dx12QueueImpl.h"

#if defined(PE_WIN32)

#include "API/Command.h"
#include "API/DX12/Dx12CommandBufferImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12SemaphoreImpl.h"
#include "API/RHI.h"
#include "API/Semaphore.h"
#include "API/Swapchain.h"

namespace pe
{
    namespace
    {
        std::mutex s_submitMutex{};

        Dx12SemaphoreImpl *GetSemaphoreImpl(Semaphore *semaphore)
        {
            return semaphore ? Dx12SemaphoreImpl::TryFrom(semaphore) : nullptr;
        }
    } // namespace

    Dx12QueueImpl::Dx12QueueImpl(Queue *owner, uint32_t, const std::string &)
        : m_owner{owner}
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetGraphicsQueue(), "Dx12QueueImpl requires an initialized DX12 graphics queue");
        m_queue = rhi->GetGraphicsQueue();
    }

    void Dx12QueueImpl::Submit(uint32_t commandBuffersCount,
                               CommandBuffer *const *commandBuffers,
                               Semaphore *wait,
                               Semaphore *signal,
                               Semaphore *submissionsSemaphore,
                               uint64_t submissionValue)
    {
        std::lock_guard<std::mutex> lock(s_submitMutex);

        if (auto *waitImpl = GetSemaphoreImpl(wait))
            waitImpl->QueueWait(m_queue, waitImpl->CurrentQueueSignalValue());

        std::vector<ID3D12CommandList *> lists;
        lists.reserve(commandBuffersCount);
        for (uint32_t i = 0; i < commandBuffersCount; ++i)
        {
            CommandBuffer *cmd = commandBuffers[i];
            if (!cmd)
                continue;

            ID3D12GraphicsCommandList *list = GetDx12CommandList(cmd);
            if (list)
                lists.push_back(list);
            cmd->SetSubmission(submissionValue);
        }

        if (!lists.empty())
            m_queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());

        if (auto *signalImpl = GetSemaphoreImpl(signal))
            signalImpl->QueueSignal(m_queue, signalImpl->NextQueueSignalValue());

        if (auto *submissionsImpl = GetSemaphoreImpl(submissionsSemaphore))
            submissionsImpl->QueueSignal(m_queue, submissionValue);
    }

    void Dx12QueueImpl::Present(Swapchain *swapchain, uint32_t, Semaphore *wait)
    {
        if (auto *waitImpl = GetSemaphoreImpl(wait))
            waitImpl->QueueWait(m_queue, waitImpl->CurrentQueueSignalValue());
        if (swapchain)
            swapchain->Present();
    }

    void Dx12QueueImpl::WaitIdle()
    {
        auto *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi, "Dx12QueueImpl::WaitIdle: DX12 RHI unavailable");
        rhi->WaitDeviceIdle();
    }
} // namespace pe

#endif // PE_WIN32
