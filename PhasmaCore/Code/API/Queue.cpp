#include "API/Queue.h"
#include "API/Semaphore.h"
#include "API/Command.h"
#include "API/Swapchain.h"
#include "API/RHI.h"
#include "API/Surface.h"

namespace pe
{
    CommandPool::CommandPool(Queue *queue, vk::CommandPoolCreateFlags flags, const std::string &name)
         : m_queue(queue), m_flags(flags)
    {
        vk::CommandPoolCreateInfo cpci{};
        cpci.queueFamilyIndex = m_queue->GetFamilyId();
        cpci.flags = flags;

        m_apiHandle = RHII.GetDevice().createCommandPool(cpci);
        Debug::SetObjectName(m_apiHandle, name);
    }

    CommandPool::~CommandPool()
    {
        if (m_apiHandle)
        {
            RHII.GetDevice().destroyCommandPool(m_apiHandle);
            m_apiHandle = vk::CommandPool{};
        }

        for (uint32_t i = 0; i < m_freeCmdStack.size(); i++)
        {
            m_freeCmdStack.top()->ApiHandle() = vk::CommandBuffer{};
            m_freeCmdStack.pop();
        }
    }

    void CommandPool::Reset()
    {
        RHII.GetDevice().resetCommandPool(m_apiHandle);
    }

    Queue::Queue(vk::Device device,
                 uint32_t familyId,
                 const std::string &name)
        : m_familyId{familyId},
          m_name{name},
          m_submissionsSemaphore{Semaphore::Create(true, name + "_submissionsSemaphore")}
    {
        m_apiHandle = RHII.GetDevice().getQueue(m_familyId, 0);
        Debug::SetObjectName(m_apiHandle, m_name);
    }

    Queue::~Queue()
    {
        for (auto &pair : m_commandPools)
        {
            for (CommandPool *pool : pair.second)
            {
                CommandPool::Destroy(pool);
            }
        }

        Semaphore::Destroy(m_submissionsSemaphore);
    }

    void Queue::Submit(uint32_t commandBuffersCount, CommandBuffer *const *commandBuffers, Semaphore *wait, Semaphore *signal)
    {
        std::lock_guard<std::mutex> lock(s_submitMutex);

        vk::SemaphoreSubmitInfo waitSemaphoreSubmitInfo{};
        std::vector<vk::SemaphoreSubmitInfo> signalSemaphoreSubmitInfos{};
        std::vector<vk::CommandBufferSubmitInfo> commandBufferSubmitInfos{};

        signalSemaphoreSubmitInfos.reserve(2);
        commandBufferSubmitInfos.reserve(commandBuffersCount);

        // wait semaphore
        if (wait)
        {
            waitSemaphoreSubmitInfo.semaphore = wait->ApiHandle();
            waitSemaphoreSubmitInfo.stageMask = wait->GetStageFlags();
        }

        // signal semaphores
        if (signal)
        {
            vk::SemaphoreSubmitInfo info{};
            info.semaphore = signal->ApiHandle();
            info.stageMask = signal->GetStageFlags();
            signalSemaphoreSubmitInfos.push_back(info);
        }

        // signal semaphore for the command buffers
        {
            vk::SemaphoreSubmitInfo info{};
            info.semaphore = m_submissionsSemaphore->ApiHandle();
            info.stageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
            info.value = ++m_submission;
            signalSemaphoreSubmitInfos.push_back(info);
        }

        // command buffers
        for (uint32_t i = 0; i < commandBuffersCount; i++)
        {
            CommandBuffer *cmd = commandBuffers[i];
            if (cmd)
            {
                vk::CommandBufferSubmitInfo cbInfo{};
                cbInfo.commandBuffer = cmd->ApiHandle();
                commandBufferSubmitInfos.push_back(cbInfo);
                cmd->SetSubmission(m_submission);
            }
        }

        vk::SubmitInfo2 si{};
        si.waitSemaphoreInfoCount = wait ? 1 : 0;
        si.pWaitSemaphoreInfos = &waitSemaphoreSubmitInfo;
        si.commandBufferInfoCount = static_cast<uint32_t>(commandBufferSubmitInfos.size());
        si.pCommandBufferInfos = commandBufferSubmitInfos.data();
        si.signalSemaphoreInfoCount = static_cast<uint32_t>(signalSemaphoreSubmitInfos.size());
        si.pSignalSemaphoreInfos = signalSemaphoreSubmitInfos.data();

        auto result = m_apiHandle.submit2(1, &si, nullptr);
        if (result != vk::Result::eSuccess)
            PE_ERROR("Failed to submit to queue!");
    }

    void Queue::Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait)
    {
        vk::PresentInfoKHR pi{};
        pi.waitSemaphoreCount = wait ? 1 : 0;
        pi.pWaitSemaphores = wait ? &wait->ApiHandle() : nullptr;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain->ApiHandle();
        pi.pImageIndices = &imageIndex;

        auto result = m_apiHandle.presentKHR(pi);
        if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
            PE_ERROR("Failed to present swapchain image!");
    }

    void Queue::Wait()
    {
        m_submissionsSemaphore->Wait(m_submission);
    }

    void Queue::WaitIdle()
    {
        m_apiHandle.waitIdle();
    }

    void Queue::BeginDebugRegion(const std::string &name)
    {
        Debug::BeginQueueRegion(this, name);
    }

    void Queue::InsertDebugLabel(const std::string &name)
    {
        Debug::InsertQueueLabel(this, name);
    }

    void Queue::EndDebugRegion()
    {
        Debug::EndQueueRegion(this);
    }

    CommandBuffer *Queue::AcquireCommandBuffer(vk::CommandPoolCreateFlags flags)
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);

        std::thread::id threadId = std::this_thread::get_id();
        auto it = m_commandPools.find(threadId);
        if (it == m_commandPools.end())
        {
            auto [it_new, inserted] = m_commandPools.emplace(threadId, std::vector<CommandPool *>());
            it = it_new;
        }
        auto &commandPools = it->second;

        // ResetCommandBuffer is always set, we use the same command pool per command buffer types
        // TODO: Find a better way to handle this, maybe reset the command pool at the end of the frame for specific command buffers
        flags |= vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        CommandPool *cp = nullptr;
        for (auto *commandPool : commandPools)
        {
            if (commandPool->GetFlags() == flags)
            {
                cp = commandPool;
                break;
            }
        }

        if (!cp)
        {
            cp = CommandPool::Create(this, flags, "CommandPool_" + m_name);
            commandPools.push_back(cp);
        }

        auto &stack = cp->m_freeCmdStack;
        if (stack.empty())
            return CommandBuffer::Create(cp, "CommandBuffer_" + m_name);

        CommandBuffer *cmd = stack.top();
        stack.pop();

        return cmd;
    }

    void Queue::ReturnCommandBuffer(CommandBuffer *cmd)
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);
        cmd->GetCommandPool()->m_freeCmdStack.push(cmd);
    }
}
