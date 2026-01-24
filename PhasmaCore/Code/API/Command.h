#pragma once

#undef MemoryBarrier

namespace pe
{
    class RenderPass;
    class Framebuffer;
    class Pipeline;
    class Compute;
    class Buffer;
    class Image;
    class Descriptor;
    class Semaphore;
    class Queue;
    class PassInfo;
    class Event;
    class CommandPool;
    struct ImageBarrierInfo;
    struct BufferBarrierInfo;

    template <class T>
    inline uint32_t GetUboDataOffset(T offset)
    {
        static_assert(std::is_integral_v<T>, "T must be integral");

        // THe UBO data buffer is using mat4 chunks, so we need to align the offset as such
        // tbuffer UBO { float4x4 data[MAX_DATA_SIZE]; };
        return static_cast<uint32_t>(offset >> 6);
    }

    struct Attachment
    {
        Image *image = nullptr;
        vk::AttachmentLoadOp loadOp = vk::AttachmentLoadOp::eClear;
        vk::AttachmentStoreOp storeOp = vk::AttachmentStoreOp::eStore;
        vk::AttachmentLoadOp stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        vk::AttachmentStoreOp stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    };

    struct PushDescriptorInfo
    {
        vk::DescriptorType type = vk::DescriptorType::eCombinedImageSampler;
        uint32_t binding = (uint32_t)-1;

        std::vector<Buffer *> buffers{};
        std::vector<size_t> offsets{};
        std::vector<size_t> ranges{}; // range of the buffers in bytes to use

        std::vector<vk::ImageLayout> layouts{};
        std::vector<vk::ImageView> views{};
        std::vector<vk::Sampler> samplers{}; // if type == DescriptorType::CombinedImageSampler, these are the samplers for each view
    };

    template <uint16_t N>
    class PushConstantsBlock
    {
        static_assert(N % 4 == 0, "N must be a multiple of 4 bytes");

    public:
        // offset for every 4 bytes count (minimum alignment), thus for floats, uint32_t, etc.
        template <class T>
        void Set(uint16_t offset, const T &value)
        {
            uint16_t offsetBytes = offset * 4;
            PE_ERROR_IF(offsetBytes + sizeof(T) > N, "PushConstants buffer overflow");
            memcpy(&m_data[offsetBytes], &value, sizeof(T));
        }
        void Reset() { memset(m_data, 0, N); }
        const void *Data() const { return m_data; }

    private:
        uint8_t m_data[N];
    };

    class CommandBuffer : public PeHandle<CommandBuffer, vk::CommandBuffer>
    {
    public:
        CommandBuffer(CommandPool *commandPool, const std::string &name);
        ~CommandBuffer();

        void Begin();
        void End();
        void Reset();
        void BlitImage(Image *src, Image *dst, const vk::ImageBlit &region, vk::Filter filter);
        void ClearColors(std::vector<Image *> images);
        void ClearDepthStencils(std::vector<Image *> images);
        void BeginPass(uint32_t count, Attachment *attachments, const std::string &name, bool skipDynamicPass = false);
        void EndPass();
        void BindPipeline(PassInfo &passInfo, bool bindDescriptors = true);
        void BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding = 0, uint32_t bindingCount = 1);
        void BindIndexBuffer(Buffer *buffer, size_t offset);
        void BindDescriptors(uint32_t count, Descriptor *const *descriptors);
        void PushDescriptor(uint32_t set, const std::vector<PushDescriptorInfo> &info);
        void SetViewport(float x, float y, float width, float height);
        void SetScissor(int x, int y, uint32_t width, uint32_t height);
        void SetLineWidth(float width);
        void SetDepthBias(float constantFactor, float clamp, float slopeFactor);
        void SetDepthTestEnable(uint32_t enable);
        void SetDepthWriteEnable(uint32_t enable);
        void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
        template <class T>
        void SetConstantAt(uint16_t offset, const T &value) { m_pushConstants.Set(offset, value); }
        template <class T>
        void SetConstants(const T &value) { m_pushConstants.Set(0, value); }
        void PushConstants();
        void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
        void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
        void DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride = sizeof(vk::DrawIndirectCommand));
        void DrawIndexedIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride = sizeof(vk::DrawIndexedIndirectCommand));
        void TraceRays(uint32_t width, uint32_t height, uint32_t depth);
        void BuildAccelerationStructures(uint32_t infoCount, const vk::AccelerationStructureBuildGeometryInfoKHR *pInfos, const vk::AccelerationStructureBuildRangeInfoKHR **ppBuildRangeInfos);
        void BufferBarrier(const BufferBarrierInfo &info);
        void BufferBarriers(const std::vector<BufferBarrierInfo> &infos);
        void ImageBarrier(const ImageBarrierInfo &info);
        void ImageBarriers(const std::vector<ImageBarrierInfo> &infos);
        void MemoryBarrier(const vk::MemoryBarrier2 &info);
        void MemoryBarriers(const std::vector<vk::MemoryBarrier2> &infos);
        void CopyBuffer(Buffer *src, Buffer *dst, const size_t size, size_t srcOffset, size_t dstOffset);
        void CopyBufferStaged(Buffer *buffer, void *data, size_t size, size_t dstOffset);
        void CopyDataToImageStaged(Image *image,
                                   void *data,
                                   size_t size,
                                   uint32_t baseArrayLayer = 0,
                                   uint32_t layerCount = 0,
                                   uint32_t mipLevel = 0);
        void CopyImage(Image *src, Image *dst);
        void GenerateMipMaps(Image *image);
        void SetEvent(Image *image,
                      vk::ImageLayout srcLayout, vk::ImageLayout dstLayout,
                      vk::PipelineStageFlags2 srcStage, vk::PipelineStageFlags2 dstStage,
                      vk::AccessFlags2 srcAccess, vk::AccessFlags2 dstAccess);
        void WaitEvent();
        void ResetEvent(vk::PipelineStageFlags2 resetStage);
        bool IsRecording() const { return m_recording; }
        void BeginDebugRegion(const std::string &name);
        void InsertDebugLabel(const std::string &name);
        void EndDebugRegion();
        uint32_t GetFamilyId() const;
        Queue *GetQueue() const;
        void SetSubmission(uint64_t submission) { m_submission = submission; }
        void Wait();
        void Return();
        CommandPool *GetCommandPool() const { return m_commandPool; }
        void AddAfterWaitCallback(Delegate<>::FunctionType &&func);
        uint32_t GetGpuTimerInfosCount() const { return m_gpuTimerInfosCount; }

        // Resourses functionality
        static RenderPass *GetRenderPass(uint32_t count, Attachment *attachments);
        static Framebuffer *GetFramebuffer(RenderPass *renderPass, uint32_t count, Attachment *attachments);
        static Pipeline *GetPipeline(RenderPass *renderPass, PassInfo &info);
        static void ClearCache();
        inline static auto &GetFramebuffers() { return s_framebuffers; }

    private:
        friend class Queue;

        void BindGraphicsPipeline();
        void BindComputePipeline();
        void BindRayTracingPipeline();
        void BindGraphicsDescriptors(uint32_t count, Descriptor *const *descriptors);
        void BindComputeDescriptors(uint32_t count, Descriptor *const *descriptors);
        void BindRayTracingDescriptors(uint32_t count, Descriptor *const *descriptors);
        void BatchBindDescriptors(vk::PipelineBindPoint point, uint32_t count, Descriptor *const *descriptors);

        // Resources
        inline static std::unordered_map<size_t, RenderPass *> s_renderPasses{};
        inline static std::unordered_map<size_t, Framebuffer *> s_framebuffers{};
        inline static std::unordered_map<size_t, Pipeline *> s_pipelines{};

        std::mutex m_WaitMutex{};
        size_t m_id;
        CommandPool *m_commandPool;
        Event *m_event;
        uint64_t m_submission;
        bool m_recording = false;
        std::string m_name;
        Delegate<> m_afterWaitCallbacks;
        uint32_t m_attachmentCount;
        Attachment *m_attachments;
        RenderPass *m_renderPass;
        Framebuffer *m_framebuffer;
        Pipeline *m_boundPipeline;
        Buffer *m_boundVertexBuffer;
        size_t m_boundVertexBufferOffset;
        uint32_t m_boundVertexBufferFirstBinding;
        uint32_t m_boundVertexBufferBindingCount;
        Buffer *m_boundIndexBuffer;
        size_t m_boundIndexBufferOffset;
        PushConstantsBlock<128> m_pushConstants{};
        bool m_dynamicPass = false;
        std::thread::id m_threadId; // the thread that created this command buffer, used for synchronization

#if PE_DEBUG_MODE
        friend class Debug;

        uint32_t m_gpuTimerInfosCount = 0;
        std::vector<GpuTimerInfo> m_gpuTimerInfos{};
        std::stack<size_t> m_gpuTimerIdsStack{};
#endif
    };
} // namespace pe
