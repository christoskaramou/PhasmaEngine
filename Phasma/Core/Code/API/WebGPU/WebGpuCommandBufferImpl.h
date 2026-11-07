#pragma once

#include "API/CommandBuffer_Internal.h"

#include <webgpu/webgpu.h>

namespace pe
{
    struct WebGpuBufferImpl;
    struct WebGpuPipelineImpl;

    // One WGPUCommandEncoder per recording. Records every rule-2 buffer (host-visible, not staging) the recording
    // references, so the queue uploads exactly those shadows before the submit (spec decision 5).
    struct WebGpuCommandBufferImpl final : public CommandBuffer::Impl
    {
        WebGpuCommandBufferImpl(CommandBuffer *owner, CommandPool *commandPool, const std::string &name);
        ~WebGpuCommandBufferImpl() override;

        static WebGpuCommandBufferImpl *From(CommandBuffer *cmd) { return static_cast<WebGpuCommandBufferImpl *>(cmd->m_impl); }

        // Queue side: upload the referenced mapped shadows, then hand over the finished command buffer.
        void UploadReferencedBuffers();
        WGPUCommandBuffer TakeCommands();

        // Every command that makes the GPU read a buffer calls this: copy sources, vertex, index and indirect buffers,
        // and the buffers in bound groups.
        void ReferenceBuffer(Buffer *buffer);

        // For encoder-level commands recorded elsewhere (image copies, blits, mips): ends an open compute pass, and
        // fails inside a render pass, where WebGPU locks the encoder.
        WGPUCommandEncoder GetEncoder() { return Encoder("an image command"); }

        void Begin() override;
        void End() override;
        void Reset() override;

        void BlitImage(Image *src, Image *dst, const ImageBlit &region, PeFilter filter) override;
        void ClearColors(std::vector<Image *> images) override;
        void ClearDepthStencils(std::vector<Image *> images) override;

        void BeginPass(uint32_t count, Attachment *attachments, const std::string &name, bool skipDynamicPass) override;
        void EndPass() override;

        void BindPipeline(PassInfo &passInfo, bool bindDescriptors) override;
        void BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding, uint32_t bindingCount) override;
        void BindIndexBuffer(Buffer *buffer, size_t offset, PeIndexType indexType) override;
        void BindDescriptors(uint32_t count, Descriptor *const *descriptors) override;
        void PushDescriptor(uint32_t set, const std::vector<PushDescriptorInfo> &info) override;

        void SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth) override;
        void SetScissor(int x, int y, uint32_t width, uint32_t height) override;
        void SetBlendConstants(const float constants[4]) override;
        void SetStencilReference(uint32_t reference) override;
        void SetLineWidth(float) override {}               // WebGPU lines are 1 pixel
        void SetDepthBias(float, float, float) override {} // baked into the pipeline (decision 5)
        void SetDepthTestEnable(uint32_t) override {}      // baked into the pipeline (decision 5)
        void SetDepthWriteEnable(uint32_t) override {}     // baked into the pipeline (decision 5)

        void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
        void DispatchIndirect(Buffer *indirectBuffer, size_t offset) override;
        void PushConstants(const PushConstantsBlock<128> &constants) override;

        void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
        void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) override;
        void DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride) override;
        void DrawIndexedIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride) override;
        void DrawIndexedIndirectCount(Buffer *indirectBuffer, size_t offset, Buffer *countBuffer, size_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride) override;

        void FillBuffer(Buffer *buffer, size_t offset, size_t size, uint32_t data) override;
        void CopyBuffer(Buffer *src, Buffer *dst, size_t size, size_t srcOffset, size_t dstOffset) override;
        void CopyBufferStaged(Buffer *buffer, void *data, size_t size, size_t dstOffset) override;
        void CopyDataToImageStaged(Image *image, void *data, size_t size, uint32_t baseArrayLayer, uint32_t layerCount, uint32_t mipLevel) override;
        void CopyImage(Image *src, Image *dst) override;
        void CopyImageToBuffer(Image *src, Buffer *dst, uint32_t mipLevel, uint32_t baseArrayLayer, uint32_t layerCount) override;
        void GenerateMipMaps(Image *image) override;

        void ResetQueryPool(QueryPool *pool, uint32_t firstQuery, uint32_t queryCount) override;
        void BeginQuery(QueryPool *pool, uint32_t queryIndex, PeQueryControlFlags flags) override;
        void EndQuery(QueryPool *pool, uint32_t queryIndex) override;
        void WriteTimestamp(QueryPool *pool, uint32_t queryIndex) override;
        void ResolveQueryPool(QueryPool *pool, uint32_t firstQuery, uint32_t queryCount,
                              Buffer *dst, uint64_t dstOffset, uint64_t stride, PeQueryResultFlags flags) override;

        void TraceRays(uint32_t width, uint32_t height, uint32_t depth) override;

        // WebGPU tracks resource usage itself; barriers are no-ops (decision 5).
        void BufferBarrier(const BufferBarrierInfo &) override {}
        void BufferBarriers(const std::vector<BufferBarrierInfo> &) override {}
        void ImageBarrier(const ImageBarrierInfo &) override {}
        void ImageBarriers(const std::vector<ImageBarrierInfo> &) override {}
        void MemoryBarrier(const MemoryBarrierInfo &) override {}
        void MemoryBarriers(const std::vector<MemoryBarrierInfo> &) override {}

        void SetEvent(Event *event, Image *image,
                      PeImageLayout srcLayout, PeImageLayout dstLayout,
                      PeBarrierSync srcStage, PeBarrierSync dstStage,
                      PeBarrierAccess srcAccess, PeBarrierAccess dstAccess) override;

        void BeginDebugRegion(const std::string &name) override;
        void InsertDebugLabel(const std::string &name) override;
        void EndDebugRegion() override;

    private:
        struct VertexBinding
        {
            Buffer *buffer = nullptr;
            uint64_t offset = 0;
        };

        void Release();
        static void CheckNotGpuWritten(Buffer *buffer, const char *what);
        WGPUCommandEncoder Encoder(const char *what);
        void EndComputePass();
        void ResetPassState();
        WebGpuPipelineImpl *BoundPipeline(bool compute, const char *what);
        WGPUBindGroup GroupAt(uint32_t index, const WebGpuPipelineImpl *pipeline);
        void FlushRenderState(const char *what);
        void FlushComputeState(const char *what);
        void FlushDynamicState();
        void FlushScissor();

        CommandBuffer *m_owner = nullptr;
        WGPUCommandEncoder m_encoder = nullptr;
        WGPUCommandBuffer m_commands = nullptr;
        // A compute pass opens at the first Dispatch and closes at the next command that is not one (decision 5).
        WGPURenderPassEncoder m_renderPass = nullptr;
        WGPUComputePassEncoder m_computePass = nullptr;
        uint32_t m_attachmentWidth = 0;
        uint32_t m_attachmentHeight = 0;
        std::vector<WebGpuBufferImpl *> m_referenced;
        std::vector<Descriptor *> m_boundDescriptors; // set index order

        // Bound state reaches the pass at the next draw or dispatch, since every WebGPU pass starts without any.
        Pipeline *m_appliedPipeline = nullptr;
        WGPUBindGroup m_appliedGroups[3] = {};
        uint32_t m_pushOffset = 0;
        uint32_t m_appliedPushOffset = UINT32_MAX;
        std::vector<VertexBinding> m_vertexBuffers;
        Buffer *m_indexBuffer = nullptr;
        uint64_t m_indexOffset = 0;
        WGPUIndexFormat m_indexFormat = WGPUIndexFormat_Uint32;
        float m_viewport[6] = {};
        int32_t m_scissorX = 0;
        int32_t m_scissorY = 0;
        uint32_t m_scissorWidth = 0;
        uint32_t m_scissorHeight = 0;
        WGPUColor m_blendConstant{};
        uint32_t m_stencilReference = 0;
        bool m_hasViewport = false;
        bool m_hasScissor = false;
        bool m_hasBlendConstant = false;
        bool m_hasStencilReference = false;
        bool m_dynamicDirty = false;

        // Debug groups nest per encoder: groups opened inside a render pass close with it, and a group opened before
        // the pass but closed inside it pops from the command encoder after the pass ends.
        uint32_t m_debugDepth = 0;
        uint32_t m_passDebugDepth = 0;
        uint32_t m_deferredPops = 0;
    };
} // namespace pe
