#pragma once

namespace pe
{
    class Semaphore;
    class CommandBuffer;
    class Swapchain;

    class Queue : public IHandle<Queue, QueueHandle>
    {
    public:
        Queue(QueueHandle handle,
              uint32_t familyId,
              QueueTypeFlags m_queueTypeFlags,
              ivec3 m_imageGranularity,
              const std::string &name);

        ~Queue();

        static void Init(GpuHandle gpu, DeviceHandle device, SurfaceHandle surface);

        static void Clear();

        static Queue *GetNext(QueueTypeFlags queueTypeFlags, int minImageGranularity);

        void Submit(uint32_t commandBuffersCount, CommandBuffer **commandBuffers,
                    PipelineStageFlags *waitStages,
                    uint32_t waitSemaphoresCount, Semaphore **waitSemaphores,
                    uint32_t signalSemaphoresCount, Semaphore **signalSemaphores);

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
        inline static std::mutex s_getNextMutex{};
        inline static std::mutex s_submitMutex{};

        uint32_t m_familyId;
        QueueTypeFlags m_queueTypeFlags;
        ivec3 m_imageGranularity;
        std::string name;
        Semaphore *m_semaphore;
        std::atomic_uint64_t m_submitions;
    };
}