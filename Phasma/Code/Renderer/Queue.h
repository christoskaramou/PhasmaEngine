/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

namespace pe
{
    class Semaphore;
    class Fence;
    class CommandBuffer;
    class Swapchain;

    enum QueueType
    {
        GraphicsBit = 1 << 0,
        ComputeBit = 1 << 1,
        TransferBit = 1 << 2,
        SparseBindingBit = 1 << 3,
        ProtectedBit = 1 << 4,
        PresentBit = 1 << 5,
        AllBits = GraphicsBit | ComputeBit | TransferBit | SparseBindingBit | ProtectedBit | PresentBit,
        AllExceptPresentBits = AllBits & ~PresentBit
    };

    class Queue : public IHandle<Queue, QueueHandle>
    {
    public:
        Queue(QueueHandle handle,
              uint32_t familyId,
              uint32_t m_queueTypeFlags,
              ivec3 m_imageGranularity,
              const std::string &name);

        ~Queue();

        static void Init(GpuHandle gpu, DeviceHandle device, SurfaceHandle surface);

        static void Clear();

        static Queue *GetNext(uint32_t queueTypeFlags, int minImageGranularity);

        static void Return(Queue *queue);

        void Submit(uint32_t commandBuffersCount, CommandBuffer **commandBuffers,
                    PipelineStageFlags *waitStages,
                    uint32_t waitSemaphoresCount, Semaphore **waitSemaphores,
                    uint32_t signalSemaphoresCount, Semaphore **signalSemaphores,
                    Fence *signalFence);

        void SubmitAndWaitFence(
            uint32_t commandBuffersCount, CommandBuffer **commandBuffers,
            PipelineStageFlags *waitStages,
            uint32_t waitSemaphoresCount, Semaphore **waitSemaphores,
            uint32_t signalSemaphoresCount, Semaphore **signalSemaphores);

        void Present(uint32_t swapchainCount, Swapchain **swapchains,
                     uint32_t *imageIndices,
                     uint32_t waitSemaphoreCount, Semaphore **waitSemaphores);

        void WaitIdle();

        uint32_t GetFamilyId() const { return m_familyId; }

        uint32_t GetQueueTypeFlags() const { return m_queueTypeFlags; }

        ivec3 GetImageGranularity() const { return m_imageGranularity; }

        std::string name;

    private:
        static void CheckFutures();

        inline static std::unordered_map<Queue *, Queue *> s_availableQueues{};
        inline static std::map<Queue *, Queue *> s_allQueues{};
        inline static std::unordered_map<Queue *, std::future<Queue *>> s_queueWaits;
        inline static std::atomic_bool s_availableQueuesReady{false};
        inline static bool killThreads{false};

        uint32_t m_familyId;
        uint32_t m_queueTypeFlags;
        ivec3 m_imageGranularity;
    };
}