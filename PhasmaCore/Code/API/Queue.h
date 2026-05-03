#pragma once

namespace pe
{
    class Semaphore;
    class CommandBuffer;
    class Swapchain;
    class Queue;

    class CommandPool : public PeHandle<CommandPool, vk::CommandPool>
    {
    public:
        struct Impl;

        CommandPool(Queue *queue, vk::CommandPoolCreateFlags flags, const std::string &name);
        ~CommandPool();

        Queue *GetQueue() { return m_queue; }
        vk::CommandPoolCreateFlags GetFlags() { return m_flags; }
        void Reset();

    private:
        friend class Queue;
        friend struct VulkanCommandPoolImpl;
#if defined(PE_WIN32)
        friend struct Dx12CommandPoolImpl;
#endif

        Impl *m_impl{};
        Queue *m_queue;
        vk::CommandPoolCreateFlags m_flags;
        std::stack<CommandBuffer *> m_freeCmdStack{};
    };

    class Queue : public PeHandle<Queue, vk::Queue>
    {
    public:
        struct Impl;

        Queue(uint32_t familyId, const std::string &name);
        ~Queue();

        void Submit(uint32_t commandBuffersCount, CommandBuffer *const *commandBuffers, Semaphore *wait, Semaphore *signal);
        void Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait);
        void Wait();
        void WaitIdle();
        void BeginDebugRegion(const std::string &name);
        void InsertDebugLabel(const std::string &name);
        void EndDebugRegion();
        uint32_t GetFamilyId() const { return m_familyId; }
        Semaphore *GetSubmissionsSemaphore() const { return m_submissionsSemaphore; }
        CommandBuffer *AcquireCommandBuffer(vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eTransient);
        void ReturnCommandBuffer(CommandBuffer *cmd);
        uint64_t GetSubmissionCount() const { return m_submission.load(std::memory_order_acquire); }

    private:
        friend struct VulkanQueueImpl;
#if defined(PE_WIN32)
        friend struct Dx12QueueImpl;
#endif

        Impl *m_impl{};
        uint32_t m_familyId;
        std::string m_name;
        std::atomic_uint64_t m_submission{0};
        Semaphore *m_submissionsSemaphore{nullptr};
        std::unordered_map<std::thread::id, std::vector<CommandPool *>> m_commandPools{};
        std::mutex m_cmdMutex{};
    };
} // namespace pe
