#include "API/WebGPU/WebGpuQueueImpl.h"
#include "API/Command.h"
#include "API/Semaphore.h"
#include "API/WebGPU/WebGpuCommandBufferImpl.h"
#include "API/WebGPU/WebGpuRhiImpl.h"

namespace pe
{
    void WebGpuQueueImpl::Submit(uint32_t commandBuffersCount,
                                 CommandBuffer *const *commandBuffers,
                                 Semaphore *,
                                 Semaphore *signal,
                                 Semaphore *submissionsSemaphore,
                                 uint64_t submissionValue)
    {
        std::vector<WGPUCommandBuffer> commands;
        commands.reserve(commandBuffersCount);
        for (uint32_t i = 0; i < commandBuffersCount; ++i)
        {
            CommandBuffer *cmd = commandBuffers[i];
            if (!cmd)
                continue;

            // Rule 2: writeBuffer lands on the queue timeline before this submit's commands.
            WebGpuCommandBufferImpl *impl = WebGpuCommandBufferImpl::From(cmd);
            impl->UploadReferencedBuffers();

            WGPUCommandBuffer list = impl->TakeCommands();
            if (list)
                commands.push_back(list);

            cmd->SetSubmission(submissionValue);
        }

        // Push-constant slots written since the last submit (decision 8), queued before these commands as well.
        WebGpuRhiImpl::Get()->UploadPush();

        if (!commands.empty())
            wgpuQueueSubmit(WebGpuRhiImpl::Get()->m_queue, commands.size(), commands.data());

        for (WGPUCommandBuffer list : commands)
            wgpuCommandBufferRelease(list);

        if (signal)
            signal->Signal(signal->GetValue() + 1);

        if (submissionsSemaphore)
            submissionsSemaphore->Signal(submissionValue);
    }

    void WebGpuQueueImpl::Present(Swapchain *, uint32_t, Semaphore *)
    {
        // The browser presents the canvas when the frame callback returns (decision 4); wgpuSurfacePresent aborts.
    }
} // namespace pe
