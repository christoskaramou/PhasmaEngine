#include "API/Queue.h"
#include "API/Command.h"
#include "API/CommandPool_Internal.h"
#include "API/Debug.h"
#include "API/Queue_Internal.h"
#include "API/RHI.h"
#include "API/Semaphore.h"
#include "API/Swapchain.h"
#include "API/Vulkan/VulkanCommandPoolImpl.h"
#include "API/Vulkan/VulkanQueueImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12CommandPoolImpl.h"
#include "API/DX12/Dx12QueueImpl.h"
#endif

namespace pe
{
    CommandPool::Impl *CreateCommandPoolImpl(CommandPool *owner, Queue *queue, PeCommandPoolCreateFlags flags, const std::string &name)
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
#if defined(PE_WIN32)
            return new Dx12CommandPoolImpl(owner, queue, flags, name);
#else
            PE_ERROR("CreateCommandPoolImpl: DX12 backend is Windows-only");
            return nullptr;
#endif
        }
        return new VulkanCommandPoolImpl(owner, queue, flags, name);
    }

    Queue::Impl *CreateQueueImpl(Queue *owner, uint32_t familyId, const std::string &name)
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
#if defined(PE_WIN32)
            return new Dx12QueueImpl(owner, familyId, name);
#else
            PE_ERROR("CreateQueueImpl: DX12 backend is Windows-only");
            return nullptr;
#endif
        }
        return new VulkanQueueImpl(owner, familyId, name);
    }

    CommandPool::CommandPool(Queue *queue, PeCommandPoolCreateFlags flags, const std::string &name)
        : m_queue(queue), m_flags(flags)
    {
        m_impl = CreateCommandPoolImpl(this, queue, flags, name);
    }

    CommandPool::~CommandPool()
    {
        delete m_impl;
        m_impl = nullptr;

        while (!m_freeCmdStack.empty())
        {
            CommandBuffer::Destroy(m_freeCmdStack.top());
            m_freeCmdStack.pop();
        }
    }

    void CommandPool::Reset()
    {
        m_impl->Reset();
    }

    Queue::Queue(uint32_t familyId, const std::string &name)
        : m_familyId{familyId},
          m_name{name},
          m_submissionsSemaphore{Semaphore::Create(true, name + "_submissionsSemaphore")}
    {
        m_impl = CreateQueueImpl(this, familyId, name);
    }

    Queue::~Queue()
    {
        Semaphore::Destroy(m_submissionsSemaphore);

        for (auto &pair : m_commandPools)
        {
            for (CommandPool *pool : pair.second)
                CommandPool::Destroy(pool);
        }

        delete m_impl;
        m_impl = nullptr;
    }

    void Queue::Submit(uint32_t commandBuffersCount, CommandBuffer *const *commandBuffers, Semaphore *wait, Semaphore *signal)
    {
        const uint64_t value = ++m_submission;
        m_impl->Submit(commandBuffersCount, commandBuffers, wait, signal, m_submissionsSemaphore, value);
    }

    void Queue::Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait)
    {
        m_impl->Present(swapchain, imageIndex, wait);
    }

    void Queue::Wait()
    {
        m_submissionsSemaphore->Wait(m_submission);
    }

    void Queue::WaitIdle()
    {
        m_impl->WaitIdle();
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

    CommandBuffer *Queue::AcquireCommandBuffer(PeCommandPoolCreateFlags flags)
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
        flags |= PE_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER;
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
} // namespace pe
