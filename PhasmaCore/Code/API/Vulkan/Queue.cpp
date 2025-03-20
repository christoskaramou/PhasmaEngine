#ifdef PE_VULKAN
#include "API/Queue.h"
#include "API/Semaphore.h"
#include "API/Command.h"
#include "API/Swapchain.h"
#include "API/RHI.h"
#include "API/Surface.h"

namespace pe
{
    CommandPool::CommandPool(Queue *queue, CommandPoolCreateFlags flags, const std::string &name)
    {
        m_queue = queue;
        m_flags = flags;

        VkCommandPoolCreateInfo cpci{};
        cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.queueFamilyIndex = m_queue->GetFamilyId();
        cpci.flags = Translate<VkCommandPoolCreateFlags>(flags);

        VkCommandPool commandPool;
        PE_CHECK(vkCreateCommandPool(RHII.GetDevice(), &cpci, nullptr, &commandPool));
        m_apiHandle = commandPool;

        Debug::SetObjectName(m_apiHandle, name);
    }

    CommandPool::~CommandPool()
    {
        if (m_apiHandle)
        {
            vkDestroyCommandPool(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }

        for (uint32_t i = 0; i < m_freeCmdStack.size(); i++)
        {
            m_freeCmdStack.top()->ApiHandle() = {};
            m_freeCmdStack.pop();
        }
    }

    void CommandPool::Reset()
    {
        PE_CHECK(vkResetCommandPool(RHII.GetDevice(), m_apiHandle, 0));
    }

    Queue::Queue(DeviceApiHandle device,
                 uint32_t familyId,
                 const std::string &name)
        : m_id{ID::NextID()},
          m_familyId{familyId},
          m_name{name},
          m_submissionsSemaphore{Semaphore::Create(true, name + "_submissionsSemaphore")}
    {
        VkQueue queueVK;
        vkGetDeviceQueue(device, familyId, 0, &queueVK);
        m_apiHandle = queueVK;

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

        VkSemaphoreSubmitInfo waitSemaphoreSubmitInfo{};
        std::vector<VkSemaphoreSubmitInfo> signalSemaphoreSubmitInfos{};
        std::vector<VkCommandBufferSubmitInfo> commandBufferSubmitInfos{};

        signalSemaphoreSubmitInfos.reserve(2);
        commandBufferSubmitInfos.reserve(commandBuffersCount);

        // wait semaphore
        if (wait)
        {
            waitSemaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            waitSemaphoreSubmitInfo.semaphore = wait->ApiHandle();
            waitSemaphoreSubmitInfo.stageMask = Translate<VkPipelineStageFlags2>(wait->GetStageFlags());
        }

        // signal semaphores
        if (signal)
        {
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = signal->ApiHandle();
            info.stageMask = Translate<VkPipelineStageFlags2>(signal->GetStageFlags());
            signalSemaphoreSubmitInfos.push_back(info);
        }

        // signal semaphore for the command buffers
        {
            VkSemaphoreSubmitInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            info.semaphore = m_submissionsSemaphore->ApiHandle();
            info.stageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            info.value = ++m_submissions;
            signalSemaphoreSubmitInfos.push_back(info);
        }

        // command buffers
        for (uint32_t i = 0; i < commandBuffersCount; i++)
        {
            CommandBuffer *cmd = commandBuffers[i];
            if (cmd)
            {
                VkCommandBufferSubmitInfo cbInfo{};
                cbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
                cbInfo.commandBuffer = cmd->ApiHandle();
                commandBufferSubmitInfos.push_back(cbInfo);
                cmd->SetSubmissions(m_submissions);
            }
        }

        VkSubmitInfo2 si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        si.waitSemaphoreInfoCount = wait ? 1 : 0;
        si.pWaitSemaphoreInfos = &waitSemaphoreSubmitInfo;
        si.commandBufferInfoCount = static_cast<uint32_t>(commandBufferSubmitInfos.size());
        si.pCommandBufferInfos = commandBufferSubmitInfos.data();
        si.signalSemaphoreInfoCount = static_cast<uint32_t>(signalSemaphoreSubmitInfos.size());
        si.pSignalSemaphoreInfos = signalSemaphoreSubmitInfos.data();

        PE_CHECK(vkQueueSubmit2(m_apiHandle, 1, &si, nullptr));
    }

    void Queue::Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait)
    {
        VkSwapchainKHR swapchainVK = swapchain->ApiHandle();
        VkSemaphore waitSemaphoreVK = VK_NULL_HANDLE;
        if (wait)
            waitSemaphoreVK = wait->ApiHandle();

        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = waitSemaphoreVK ? 1 : 0;
        pi.pWaitSemaphores = &waitSemaphoreVK;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchainVK;
        pi.pImageIndices = &imageIndex;

        PE_CHECK(vkQueuePresentKHR(m_apiHandle, &pi));
    }

    void Queue::Wait()
    {
        m_submissionsSemaphore->Wait(m_submissions);
    }

    void Queue::WaitIdle()
    {
        PE_CHECK(vkQueueWaitIdle(m_apiHandle));
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

    CommandBuffer *Queue::GetCommandBuffer(CommandPoolCreateFlags flags)
    {
        std::lock_guard<std::mutex> lock(m_cmdMutex);

        auto it = m_commandPools.find(std::this_thread::get_id());
        if (it == m_commandPools.end())
        {
            auto [it_new, inserted] = m_commandPools.emplace(std::this_thread::get_id(), std::vector<CommandPool *>());
            it = it_new;
        }
        auto &commandPools = it->second;

        // ResetCommandBuffer is always set, we use the same command pool per command buffer types
        // TODO: Find a better way to handle this, maybe reset the command pool at the end of the frame for specific command buffers
        flags |= CommandPoolCreate::ResetCommandBuffer;
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
#endif
