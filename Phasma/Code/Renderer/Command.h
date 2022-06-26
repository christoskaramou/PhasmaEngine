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
    class RenderPass;
    class FrameBuffer;
    class Pipeline;
    class Compute;
    class Buffer;
    class Image;
    class Descriptor;
    class Semaphore;

    struct ImageSubresourceLayers
    {
        ImageAspectFlags aspectMask;
        uint32_t mipLevel;
        uint32_t baseArrayLayer;
        uint32_t layerCount;
    };

    struct ImageBlit
    {
        ImageSubresourceLayers srcSubresource;
        Offset3D srcOffsets[2];
        ImageSubresourceLayers dstSubresource;
        Offset3D dstOffsets[2];
    };

    class CommandPool : public IHandle<CommandPool, CommandPoolHandle>
    {
    public:
        CommandPool(uint32_t familyId, const std::string &name);

        ~CommandPool();

        static void Init(GpuHandle gpu);

        static void Clear();

        static CommandPool *GetNext(uint32_t familyId);

        static void Return(CommandPool *commandPool);

        uint32_t GetFamilyId() const { return m_familyId; }

    private:
        inline static std::vector<std::unordered_map<CommandPool *, CommandPool *>> s_availableCps{};
        inline static std::vector<std::unordered_map<CommandPool *, CommandPool *>> s_allCps{};
        inline static std::mutex s_getNextMutex{};
        inline static std::mutex s_returnMutex{};

    private:
        uint32_t m_familyId;
    };

    class CommandBuffer : public IHandle<CommandBuffer, CommandBufferHandle>
    {
    public:
        CommandBuffer(uint32_t familyId, const std::string &name);

        ~CommandBuffer();

        void Begin();

        void End();

        void PipelineBarrier();

        void SetDepthBias(float constantFactor, float clamp, float slopeFactor);

        void BlitImage(Image *src, Image *dst, ImageBlit *region, Filter filter);

        void BeginPass(RenderPass *pass, FrameBuffer *frameBuffer);

        void EndPass();

        void BindPipeline(Pipeline *pipeline);

        void BindComputePipeline(Pipeline *pipeline);

        void BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding = 0, uint32_t bindingCount = 1);

        void BindIndexBuffer(Buffer *buffer, size_t offset);

        void BindDescriptors(Pipeline *pipeline, uint32_t count, Descriptor **descriptors);

        void BindComputeDescriptors(Pipeline *pipeline, uint32_t count, Descriptor **descriptors);

        void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

        void PushConstants(Pipeline *pipeline, ShaderStageFlags stage, uint32_t offset, uint32_t size,
                           const void *pValues);

        void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);

        void DrawIndexed(uint32_t indexCount,
                         uint32_t instanceCount,
                         uint32_t firstIndex,
                         int32_t vertexOffset,
                         uint32_t firstInstance);

        void Submit(Queue *queue,
                    PipelineStageFlags *waitStages,
                    uint32_t waitSemaphoresCount, Semaphore **waitSemaphores,
                    uint32_t signalSemaphoresCount, Semaphore **signalSemaphores);

        void ImageBarrier(Image *image,
                          ImageLayout newLayout,
                          uint32_t baseArrayLayer = 0,
                          uint32_t arrayLayers = 1,
                          uint32_t baseMipLevel = 0,
                          uint32_t mipLevels = 1);

        void CopyBuffer(Buffer *src, Buffer *dst, const size_t size, size_t srcOffset, size_t dstOffset);

        void CopyBufferStaged(Buffer *buffer, void *data, size_t size, size_t dtsOffset);

        void CopyDataToImageStaged(Image *image,
                                   void *data,
                                   size_t size,
                                   uint32_t baseArrayLayer = 0,
                                   uint32_t layerCount = 1,
                                   uint32_t mipLevel = 0);

        void CopyImage(Image *src, Image *dst);

        void GenerateMipMaps(Image *image);

        bool IsRecording() const { return m_recording; }

        void BeginDebugRegion(const std::string &name);

        void InsertDebugLabel(const std::string &name);

        void EndDebugRegion();

        uint32_t GetFamilyId() const { return m_familyId; }

        Semaphore *GetSemaphore() const { return m_semaphore; }

        uint64_t GetSumbitionCount() const { return m_submitions; }

        void IncreaceSubmitionCount() { m_submitions++; }

        void Wait();

        CommandPool *GetCommandPool() const { return m_commandPool; }

        void AddAfterWaitCallback(Delegate<>::Func_type &&func);

        static void Init(GpuHandle gpu, uint32_t countPerFamily = 0);

        static void Clear();

        static CommandBuffer *GetNext(uint32_t familyId);

        static void Return(CommandBuffer *cmd);

    private:
        friend class Queue;

        inline static std::vector<std::unordered_map<size_t, CommandBuffer *>> s_availableCmds{};
        inline static std::vector<std::map<size_t, CommandBuffer *>> s_allCmds{};
        inline static std::mutex s_getNextMutex{};
        inline static std::mutex s_returnMutex{};
        inline static std::mutex s_WaitMutex{};

        CommandPool *m_commandPool;
        uint32_t m_familyId;
        Semaphore *m_semaphore;
        std::atomic_uint64_t m_submitions;
        bool m_recording = false;
        std::string name;
        Delegate<> m_afterWaitCallbacks;
    };
}
