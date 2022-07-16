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

        static void Return(Queue *queue);

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
        inline static std::unordered_map<QueueTypeFlags::Type, std::unordered_map<size_t, Queue *>> s_availableQueues{};
        inline static std::map<QueueTypeFlags::Type, std::map<size_t, Queue *>> s_allQueues{};
        inline static std::mutex s_getNextMutex{};
        inline static std::mutex s_returnMutex{};

        uint32_t m_familyId;
        QueueTypeFlags m_queueTypeFlags;
        ivec3 m_imageGranularity;
        std::string name;
    };
}