#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/CommandBuffer_Internal.h"

namespace pe
{
    struct VulkanCommandBufferImpl final : public CommandBuffer::Impl
    {
        VulkanCommandBufferImpl(CommandBuffer *owner, CommandPool *commandPool, const std::string &name);
        ~VulkanCommandBufferImpl() override;

        static VulkanCommandBufferImpl *From(CommandBuffer *cmd) { return static_cast<VulkanCommandBufferImpl *>(cmd->m_impl); }
        static const VulkanCommandBufferImpl *From(const CommandBuffer *cmd) { return static_cast<const VulkanCommandBufferImpl *>(cmd->m_impl); }

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

        void SetViewport(float x, float y, float width, float height) override;
        void SetScissor(int x, int y, uint32_t width, uint32_t height) override;
        void SetLineWidth(float width) override;
        void SetDepthBias(float constantFactor, float clamp, float slopeFactor) override;
        void SetDepthTestEnable(uint32_t enable) override;
        void SetDepthWriteEnable(uint32_t enable) override;

        void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
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
        void CopyImageToBuffer(Image *src, Buffer *dst) override;
        void GenerateMipMaps(Image *image) override;

        void TraceRays(uint32_t width, uint32_t height, uint32_t depth) override;

        void BufferBarrier(const BufferBarrierInfo &info) override;
        void BufferBarriers(const std::vector<BufferBarrierInfo> &infos) override;
        void ImageBarrier(const ImageBarrierInfo &info) override;
        void ImageBarriers(const std::vector<ImageBarrierInfo> &infos) override;
        void MemoryBarrier(const MemoryBarrierInfo &info) override;
        void MemoryBarriers(const std::vector<MemoryBarrierInfo> &infos) override;

        void SetEvent(Event *event, Image *image,
                      PeImageLayout srcLayout, PeImageLayout dstLayout,
                      PeBarrierSync srcStage, PeBarrierSync dstStage,
                      PeBarrierAccess srcAccess, PeBarrierAccess dstAccess) override;

        void BeginDebugRegion(const std::string &name) override;
        void InsertDebugLabel(const std::string &name) override;
        void EndDebugRegion() override;

        CommandBuffer *m_owner{};
        vk::CommandBuffer m_apiHandle{};
    };

    inline vk::CommandBuffer GetVulkanCommandBuffer(CommandBuffer *cmd)
    {
        return cmd ? VulkanCommandBufferImpl::From(cmd)->m_apiHandle : vk::CommandBuffer{};
    }

    inline vk::CommandBuffer GetVulkanCommandBuffer(const CommandBuffer *cmd)
    {
        return cmd ? VulkanCommandBufferImpl::From(cmd)->m_apiHandle : vk::CommandBuffer{};
    }
} // namespace pe
