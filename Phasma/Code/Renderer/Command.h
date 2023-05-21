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
    class PassInfo;

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

    struct AttachmentInfo
    {
        Image *image = nullptr;
        AttachmentLoadOp loadOp = AttachmentLoadOp::Clear;
        AttachmentStoreOp storeOp = AttachmentStoreOp::Store;
        ImageLayout initialLayout = ImageLayout::Undefined;
        ImageLayout finalLayout = ImageLayout::ColorAttachment;
    };

    class CommandPool : public IHandle<CommandPool, CommandPoolHandle>
    {
    public:
        CommandPool(uint32_t familyId, CommandPoolCreateFlags flags, const std::string &name);

        ~CommandPool();

        static void Init(GpuHandle gpu);

        static void Clear();

        static CommandPool *GetNext(uint32_t familyId);

        static void Return(CommandPool *commandPool);

        uint32_t GetFamilyId() const { return m_familyId; }

        CommandPoolCreateFlags GetFlags() { return m_flags; }

    private:
        inline static std::vector<std::unordered_map<CommandPool *, CommandPool *>> s_availableCps{};
        inline static std::vector<std::unordered_map<CommandPool *, CommandPool *>> s_allCps{};
        inline static std::mutex s_getNextMutex{};
        inline static std::mutex s_returnMutex{};

    private:
        uint32_t m_familyId;
        CommandPoolCreateFlags m_flags;
    };

    class CommandBuffer : public IHandle<CommandBuffer, CommandBufferHandle>
    {
    public:
        CommandBuffer(uint32_t familyId, const std::string &name);

        ~CommandBuffer();

        void Begin();

        void End();

        void Reset();

        void PipelineBarrier();

        void SetDepthBias(float constantFactor, float clamp, float slopeFactor);

        void BlitImage(Image *src, Image *dst, ImageBlit *region, Filter filter);

        void BeginPass(RenderPass *renderPass, Image **colorTargets, Image *depthTarget);

        void EndPass();

        void BindPipeline(const PassInfo &passInfo);

        void BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding = 0, uint32_t bindingCount = 1);

        void BindIndexBuffer(Buffer *buffer, size_t offset);

        void BindDescriptors(uint32_t count, Descriptor **descriptors);

        void BindComputeDescriptors(uint32_t count, Descriptor **descriptors);

        void SetViewport(float x, float y, float width, float height);

        void SetScissor(int x, int y, uint32_t width, uint32_t height);

        void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

        void PushConstants(ShaderStageFlags stage, uint32_t offset, uint32_t size,
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
                          uint32_t arrayLayers = 0,
                          uint32_t baseMipLevel = 0,
                          uint32_t mipLevels = 0);

        void CopyBuffer(Buffer *src, Buffer *dst, const size_t size, size_t srcOffset, size_t dstOffset);

        void CopyBufferStaged(Buffer *buffer, void *data, size_t size, size_t dtsOffset);

        void CopyDataToImageStaged(Image *image,
                                   void *data,
                                   size_t size,
                                   uint32_t baseArrayLayer = 0,
                                   uint32_t layerCount = 0,
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

        // Cached resourses functionality
        static RenderPass *GetRenderPass(uint32_t count, AttachmentInfo *colorInfos, AttachmentInfo *depthInfo);
        static FrameBuffer *GetFrameBuffer(RenderPass *renderPass, Image **colorTargets, Image *depthTarget);
        static Pipeline *GetPipeline(const PassInfo &info);

    private:
        void BindGraphicsPipeline(Pipeline *pipeline);

        void BindComputePipeline(Pipeline *pipeline);
        
        friend class Queue;

        inline static std::vector<std::unordered_map<size_t, CommandBuffer *>> s_availableCmds{};
        inline static std::vector<std::map<size_t, CommandBuffer *>> s_allCmds{};
        inline static std::mutex s_getNextMutex{};
        inline static std::mutex s_returnMutex{};
        inline static std::mutex s_WaitMutex{};
        inline static std::map<size_t, RenderPass *> s_renderPasses{};
        inline static std::map<size_t, FrameBuffer *> s_frameBuffers{};
        inline static std::map<size_t, Pipeline *> s_pipelines{};

        CommandPool *m_commandPool;
        uint32_t m_familyId;
        Semaphore *m_semaphore;
        std::atomic_uint64_t m_submitions;
        bool m_recording = false;
        std::string m_name;
        Delegate<> m_afterWaitCallbacks;
        bool m_dynamicPass = false;
        Pipeline *m_boundPipeline;
    };
}
