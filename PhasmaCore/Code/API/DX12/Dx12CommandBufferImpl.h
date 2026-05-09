#pragma once

#include "API/CommandBuffer_Internal.h"

#if defined(PE_WIN32)

#include <d3d12.h>
#include <wrl/client.h>

#undef MemoryBarrier

namespace pe
{
    struct Dx12ImageImpl;

    struct Dx12CommandBufferImpl final : public CommandBuffer::Impl
    {
        Dx12CommandBufferImpl(CommandBuffer *owner, CommandPool *commandPool, const std::string &name);
        ~Dx12CommandBufferImpl() override = default;

        static Dx12CommandBufferImpl *From(CommandBuffer *cmd) { return static_cast<Dx12CommandBufferImpl *>(cmd->m_impl); }
        static const Dx12CommandBufferImpl *From(const CommandBuffer *cmd) { return static_cast<const Dx12CommandBufferImpl *>(cmd->m_impl); }

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

        ID3D12GraphicsCommandList *Get() const { return m_cmdList.Get(); }

        void FlushBarriers();
        void InvalidateShaderVisibleHeapBinding() { m_heapsBound = false; }

        CommandBuffer *m_owner{};
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmdList;
        std::vector<D3D12_RESOURCE_BARRIER> m_barrierBatch;

    private:
        void BindShaderVisibleHeaps();
        ID3D12CommandSignature *GetDrawIndirectSignature(uint32_t stride);
        ID3D12CommandSignature *GetDrawIndexedIndirectSignature(uint32_t stride);
        void MarkPendingImageBarrierRegion(const char *name);
        void PrepareImageForFirstWrite(Dx12ImageImpl *img, D3D12_RESOURCE_STATES requestedState, PeBarrierAccess accessMask);

        bool m_heapsBound = false;
        D3D12_PRIMITIVE_TOPOLOGY m_lastTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_drawIndirectSignature;
        Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_drawIndexedIndirectSignature;
        uint32_t m_drawIndirectSignatureStride = 0;
        uint32_t m_drawIndexedIndirectSignatureStride = 0;
        std::string m_pendingImageBarrierRegion;
    };

    inline ID3D12GraphicsCommandList *GetDx12CommandList(CommandBuffer *cmd)
    {
        return cmd ? Dx12CommandBufferImpl::From(cmd)->Get() : nullptr;
    }
} // namespace pe

#endif // PE_WIN32
