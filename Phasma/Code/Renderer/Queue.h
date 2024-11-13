#pragma once

namespace pe
{
    class Semaphore;
    class CommandBuffer;
    class Swapchain;

    class Queue : public PeHandle<Queue, QueueApiHandle>
    {
    public:
        static void Init(GpuApiHandle gpu, DeviceApiHandle device, SurfaceApiHandle surface);
        static void Clear();
        static Queue *Get(QueueTypeFlags queueTypeFlags, int minImageGranularity);
        static Semaphore *SubmitCommands(Semaphore *wait, std::vector<CommandBuffer *> cmds);

        Queue(QueueApiHandle handle,
              uint32_t familyId,
              QueueTypeFlags m_queueTypeFlags,
              ivec3 m_imageGranularity,
              const std::string &name);
        ~Queue();

        void Submit(uint32_t commandBuffersCount, CommandBuffer **commandBuffers,
                    uint32_t waitSemaphoresCount,
                    PipelineStageFlags *waitStages, Semaphore **waitSemaphores,
                    uint32_t signalSemaphoresCount,
                    PipelineStageFlags *signalStages, Semaphore **signalSemaphores);
        void Present(uint32_t swapchainCount, Swapchain **swapchains,
                     uint32_t *imageIndices,
                     uint32_t waitSemaphoreCount, Semaphore **waitSemaphores);
        void WaitIdle();
        void BeginDebugRegion(const std::string &name);
        void InsertDebugLabel(const std::string &name);
        void EndDebugRegion();
        uint32_t GetFamilyId() const { return m_familyId; }
        QueueTypeFlags GetQueueTypeFlags() const { return m_queueTypeFlags; }
        ivec3 GetImageGranularity() const { return m_imageGranularity; }

    private:
        inline static std::vector<QueueTypeFlags::Type> s_allFlags{};
        inline static std::unordered_map<QueueTypeFlags::Type, std::unordered_map<size_t, Queue *>> s_allQueues{};
        inline static std::mutex s_getMutex{};
        inline static std::mutex s_submitMutex{};

        size_t m_id;
        uint32_t m_familyId;
        QueueTypeFlags m_queueTypeFlags;
        ivec3 m_imageGranularity;
        std::string m_name;
        Semaphore *m_semaphore;
        std::atomic_uint64_t m_submitions;

#if PE_DEBUG_MODE
        friend class GUI;
        std::vector<std::vector<CommandBuffer *>> m_frameCmdsSubmitted{};
#endif
    };
}
