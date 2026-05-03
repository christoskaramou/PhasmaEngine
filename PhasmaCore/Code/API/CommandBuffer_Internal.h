#pragma once

#include "API/Command.h"

namespace pe
{
    struct CommandBuffer::Impl : public NoCopy
    {
        virtual ~Impl() = default;

        // Recording lifecycle
        virtual void Begin() = 0;
        virtual void End() = 0;
        virtual void Reset() = 0;

        // Clears + blits
        virtual void BlitImage(Image *src, Image *dst, const ImageBlit &region, PeFilter filter) = 0;
        virtual void ClearColors(std::vector<Image *> images) = 0;
        virtual void ClearDepthStencils(std::vector<Image *> images) = 0;

        // Render pass
        virtual void BeginPass(uint32_t count, Attachment *attachments, const std::string &name, bool skipDynamicPass) = 0;
        virtual void EndPass() = 0;

        // Pipeline + bindings
        virtual void BindPipeline(PassInfo &passInfo, bool bindDescriptors) = 0;
        virtual void BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding, uint32_t bindingCount) = 0;
        virtual void BindIndexBuffer(Buffer *buffer, size_t offset) = 0;
        virtual void BindDescriptors(uint32_t count, Descriptor *const *descriptors) = 0;
        virtual void PushDescriptor(uint32_t set, const std::vector<PushDescriptorInfo> &info) = 0;

        // Dynamic state
        virtual void SetViewport(float x, float y, float width, float height) = 0;
        virtual void SetScissor(int x, int y, uint32_t width, uint32_t height) = 0;
        virtual void SetLineWidth(float width) = 0;
        virtual void SetDepthBias(float constantFactor, float clamp, float slopeFactor) = 0;
        virtual void SetDepthTestEnable(uint32_t enable) = 0;
        virtual void SetDepthWriteEnable(uint32_t enable) = 0;

        // Compute + push constants
        virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
        virtual void PushConstants(const PushConstantsBlock<128> &constants) = 0;

        // Draw + indirect
        virtual void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
        virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
        virtual void DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride) = 0;
        virtual void DrawIndexedIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride) = 0;
        virtual void DrawIndexedIndirectCount(Buffer *indirectBuffer, size_t offset, Buffer *countBuffer, size_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride) = 0;

        // Transfer
        virtual void FillBuffer(Buffer *buffer, size_t offset, size_t size, uint32_t data) = 0;
        virtual void CopyBuffer(Buffer *src, Buffer *dst, size_t size, size_t srcOffset, size_t dstOffset) = 0;
        virtual void CopyBufferStaged(Buffer *buffer, void *data, size_t size, size_t dstOffset) = 0;
        virtual void CopyDataToImageStaged(Image *image, void *data, size_t size, uint32_t baseArrayLayer, uint32_t layerCount, uint32_t mipLevel) = 0;
        virtual void CopyImage(Image *src, Image *dst) = 0;
        virtual void CopyImageToBuffer(Image *src, Buffer *dst) = 0;
        virtual void GenerateMipMaps(Image *image) = 0;

        // Ray tracing
        virtual void TraceRays(uint32_t width, uint32_t height, uint32_t depth) = 0;
        // BuildAccelerationStructures is Vulkan-private; declared in Vulkan/AccelerationStructure.h
        // and called by Vulkan-private RT code only. Not part of the neutral CommandBuffer surface.

        // Barriers
        virtual void BufferBarrier(const BufferBarrierInfo &info) = 0;
        virtual void BufferBarriers(const std::vector<BufferBarrierInfo> &infos) = 0;
        virtual void ImageBarrier(const ImageBarrierInfo &info) = 0;
        virtual void ImageBarriers(const std::vector<ImageBarrierInfo> &infos) = 0;
        virtual void MemoryBarrier(const MemoryBarrierInfo &info) = 0;
        virtual void MemoryBarriers(const std::vector<MemoryBarrierInfo> &infos) = 0;

        // Events
        virtual void SetEvent(Event *event, Image *image,
                              PeImageLayout srcLayout, PeImageLayout dstLayout,
                              PeBarrierSync srcStage, PeBarrierSync dstStage,
                              PeBarrierAccess srcAccess, PeBarrierAccess dstAccess) = 0;

        // Debug labels
        virtual void BeginDebugRegion(const std::string &name) = 0;
        virtual void InsertDebugLabel(const std::string &name) = 0;
        virtual void EndDebugRegion() = 0;
    };

    CommandBuffer::Impl *CreateCommandBufferImpl(CommandBuffer *owner, CommandPool *commandPool, const std::string &name);
} // namespace pe
