#pragma once

namespace pe
{
    class Semaphore;
    class CommandBuffer;
    class Swapchain;
    class Queue;

    class CommandPool : public PeHandle<CommandPool, CommandPoolApiHandle>
    {
    public:
        CommandPool(Queue *queue, CommandPoolCreateFlags flags, const std::string &name);
        ~CommandPool();

        Queue *GetQueue() { return m_queue; }
        CommandPoolCreateFlags GetFlags() { return m_flags; }
        void Reset();

    private:
        friend class Queue;

        Queue *m_queue;
        CommandPoolCreateFlags m_flags;
        std::stack<CommandBuffer *> m_freeCmdStack{};
    };

    class Queue : public PeHandle<Queue, QueueApiHandle>
    {
    public:
        static void Init(GpuApiHandle gpu, DeviceApiHandle device, SurfaceApiHandle surface);
        static void Clear();

        Queue(QueueApiHandle handle,
              uint32_t familyId,
              QueueTypeFlags m_queueTypeFlags,
              ivec3 m_imageGranularity,
              const std::string &name);
        ~Queue();

        void Submit(uint32_t commandBuffersCount, CommandBuffer *const *commandBuffers, Semaphore *wait, Semaphore *signal);
        void Present(Swapchain *swapchain, uint32_t imageIndex, Semaphore *wait);
        void Wait();
        void WaitIdle();
        void BeginDebugRegion(const std::string &name);
        void InsertDebugLabel(const std::string &name);
        void EndDebugRegion();
        uint32_t GetFamilyId() const { return m_familyId; }
        QueueTypeFlags GetQueueTypeFlags() const { return m_queueTypeFlags; }
        ivec3 GetImageGranularity() const { return m_imageGranularity; }
        Semaphore *GetSubmissionsSemaphore() const { return m_submissionsSemaphore; }
        CommandBuffer *GetCommandBuffer(CommandPoolCreateFlags flags);
        void ReturnCommandBuffer(CommandBuffer *cmd);

    private:
        friend class RHI;
        static Queue *Get(QueueTypeFlags queueTypeFlags, int minImageGranularity, const std::vector<Queue *> &exclude = {});

        inline static std::vector<QueueTypeFlags::Type> s_allFlags{};
        inline static std::unordered_map<QueueTypeFlags::Type, std::unordered_map<size_t, Queue *>> s_allQueues{};
        inline static std::mutex s_getMutex{};
        inline static std::mutex s_submitMutex{};

        size_t m_id;
        uint32_t m_familyId;
        QueueTypeFlags m_queueTypeFlags;
        ivec3 m_imageGranularity;
        std::string m_name;
        std::atomic_uint64_t m_submissions{0};
        Semaphore *m_submissionsSemaphore{nullptr};
        std::unordered_map<std::thread::id, std::vector<CommandPool *>> m_commandPools{};
        std::mutex m_cmdMutex{};

#if PE_DEBUG_MODE
        friend class GUI;
        std::vector<std::vector<CommandBuffer *>> m_frameCmdsSubmitted{};
#endif
    };
}
