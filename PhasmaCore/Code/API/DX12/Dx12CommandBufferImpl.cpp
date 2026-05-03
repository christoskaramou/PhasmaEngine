#include "API/DX12/Dx12CommandBufferImpl.h"

#if defined(PE_WIN32)

#include "API/DX12/Dx12ImageImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12Translate.h"
#include "API/Image.h"
#include "API/RHI.h"

namespace pe
{
    using namespace pe_dx12;

    Dx12CommandBufferImpl::Dx12CommandBufferImpl(CommandBuffer *owner, CommandPool *commandPool, const std::string &name)
        : m_owner{owner}
    {
        // T10b conservative slice: CommandPool is not yet pImpl-carved on the DX12 side
        // (Task 11). For now we own a per-CommandBuffer ID3D12CommandAllocator so the
        // recording surface compiles and runs in isolation. CommandPool ownership of
        // allocators lands with Dx12CommandPoolImpl in Task 11.
        (void)commandPool;
        (void)name;

        Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        ID3D12Device *device = rhi->GetDevice();

        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator))))
            PE_ERROR("Dx12CommandBufferImpl: CreateCommandAllocator failed");

        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             m_allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&m_cmdList))))
            PE_ERROR("Dx12CommandBufferImpl: CreateCommandList failed");

        // Newly created lists are open; close so Begin() can Reset() them symmetrically.
        PE_CHECK(m_cmdList->Close());
    }

    void Dx12CommandBufferImpl::Begin()
    {
        PE_ERROR_IF(m_owner->m_recording, "CommandBuffer::Begin: CommandBuffer is already recording!");
        PE_ERROR_IF(m_owner->m_threadId != std::this_thread::get_id(), "CommandBuffer::Begin: CommandBuffer is used in a different thread!");

        Reset();

        PE_CHECK(m_allocator->Reset());
        PE_CHECK(m_cmdList->Reset(m_allocator.Get(), nullptr));

        m_owner->m_recording = true;
    }

    void Dx12CommandBufferImpl::End()
    {
        PE_ERROR_IF(!m_owner->m_recording, "CommandBuffer::End: CommandBuffer is not in recording state!");
        PE_ERROR_IF(m_owner->m_threadId != std::this_thread::get_id(), "CommandBuffer::End: CommandBuffer is used in a different thread!");

        FlushBarriers();
        PE_CHECK(m_cmdList->Close());
        m_owner->m_recording = false;
    }

    void Dx12CommandBufferImpl::Reset()
    {
        m_owner->m_attachmentCount = 0;
        m_owner->m_attachments = nullptr;
        m_owner->m_renderPass = nullptr;
        m_owner->m_framebuffer = nullptr;
        m_owner->m_dynamicPass = false;
        m_owner->m_boundPipeline = nullptr;
        m_owner->m_boundVertexBuffer = nullptr;
        m_owner->m_boundVertexBufferOffset = -1;
        m_owner->m_boundVertexBufferFirstBinding = UINT32_MAX;
        m_owner->m_boundVertexBufferBindingCount = UINT32_MAX;
        m_owner->m_boundIndexBuffer = nullptr;
        m_owner->m_boundIndexBufferOffset = -1;

        if (!m_owner->m_afterWaitCallbacks.IsEmpty())
        {
            m_owner->m_afterWaitCallbacks.ReverseInvoke();
            m_owner->m_afterWaitCallbacks.Clear();
        }

        m_barrierBatch.clear();
    }

    void Dx12CommandBufferImpl::FlushBarriers()
    {
        if (m_barrierBatch.empty())
            return;

        m_cmdList->ResourceBarrier(static_cast<UINT>(m_barrierBatch.size()), m_barrierBatch.data());
        m_barrierBatch.clear();
    }

    // ----------------------------------------------------------------------
    // Conservative T10b slice: every recording surface below is a stub. The
    // DX12 raw bypass (Dx12RhiImpl::DrawClearScreen) still drives all on-screen
    // work; Dx12CommandBufferImpl is wired but never instantiated by production
    // paths because PhasmaEditor's App early-returns under PE_GRAPHICS_API_DX12.
    // Each method gets a real implementation in subsequent T10b slices.
    // ----------------------------------------------------------------------

#define DX12_CMD_NOT_IMPLEMENTED(name) PE_ERROR("Dx12CommandBufferImpl::" name " not implemented (T10b in progress)")

    void Dx12CommandBufferImpl::BlitImage(Image *, Image *, const ImageBlit &, PeFilter)
    {
        DX12_CMD_NOT_IMPLEMENTED("BlitImage");
    }
    void Dx12CommandBufferImpl::ClearColors(std::vector<Image *>)
    {
        DX12_CMD_NOT_IMPLEMENTED("ClearColors");
    }
    void Dx12CommandBufferImpl::ClearDepthStencils(std::vector<Image *>)
    {
        DX12_CMD_NOT_IMPLEMENTED("ClearDepthStencils");
    }

    void Dx12CommandBufferImpl::BeginPass(uint32_t, Attachment *, const std::string &, bool)
    {
        DX12_CMD_NOT_IMPLEMENTED("BeginPass");
    }
    void Dx12CommandBufferImpl::EndPass()
    {
        DX12_CMD_NOT_IMPLEMENTED("EndPass");
    }

    void Dx12CommandBufferImpl::BindPipeline(PassInfo &, bool)
    {
        DX12_CMD_NOT_IMPLEMENTED("BindPipeline");
    }
    void Dx12CommandBufferImpl::BindVertexBuffer(Buffer *, size_t, uint32_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("BindVertexBuffer");
    }
    void Dx12CommandBufferImpl::BindIndexBuffer(Buffer *, size_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("BindIndexBuffer");
    }
    void Dx12CommandBufferImpl::BindDescriptors(uint32_t, Descriptor *const *)
    {
        DX12_CMD_NOT_IMPLEMENTED("BindDescriptors");
    }
    void Dx12CommandBufferImpl::PushDescriptor(uint32_t, const std::vector<PushDescriptorInfo> &)
    {
        DX12_CMD_NOT_IMPLEMENTED("PushDescriptor");
    }

    void Dx12CommandBufferImpl::SetViewport(float, float, float, float)
    {
        DX12_CMD_NOT_IMPLEMENTED("SetViewport");
    }
    void Dx12CommandBufferImpl::SetScissor(int, int, uint32_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("SetScissor");
    }
    void Dx12CommandBufferImpl::SetLineWidth(float)
    {
        DX12_CMD_NOT_IMPLEMENTED("SetLineWidth");
    }
    void Dx12CommandBufferImpl::SetDepthBias(float, float, float)
    {
        DX12_CMD_NOT_IMPLEMENTED("SetDepthBias");
    }
    void Dx12CommandBufferImpl::SetDepthTestEnable(uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("SetDepthTestEnable");
    }
    void Dx12CommandBufferImpl::SetDepthWriteEnable(uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("SetDepthWriteEnable");
    }

    void Dx12CommandBufferImpl::Dispatch(uint32_t, uint32_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("Dispatch");
    }
    void Dx12CommandBufferImpl::PushConstants(const PushConstantsBlock<128> &)
    {
        DX12_CMD_NOT_IMPLEMENTED("PushConstants");
    }

    void Dx12CommandBufferImpl::Draw(uint32_t, uint32_t, uint32_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("Draw");
    }
    void Dx12CommandBufferImpl::DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("DrawIndexed");
    }
    void Dx12CommandBufferImpl::DrawIndirect(Buffer *, size_t, uint32_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("DrawIndirect");
    }
    void Dx12CommandBufferImpl::DrawIndexedIndirect(Buffer *, size_t, uint32_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("DrawIndexedIndirect");
    }
    void Dx12CommandBufferImpl::DrawIndexedIndirectCount(Buffer *, size_t, Buffer *, size_t, uint32_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("DrawIndexedIndirectCount");
    }

    void Dx12CommandBufferImpl::FillBuffer(Buffer *, size_t, size_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("FillBuffer");
    }
    void Dx12CommandBufferImpl::CopyBuffer(Buffer *, Buffer *, size_t, size_t, size_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("CopyBuffer");
    }
    void Dx12CommandBufferImpl::CopyBufferStaged(Buffer *, void *, size_t, size_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("CopyBufferStaged");
    }
    void Dx12CommandBufferImpl::CopyDataToImageStaged(Image *, void *, size_t, uint32_t, uint32_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("CopyDataToImageStaged");
    }
    void Dx12CommandBufferImpl::CopyImage(Image *, Image *)
    {
        DX12_CMD_NOT_IMPLEMENTED("CopyImage");
    }
    void Dx12CommandBufferImpl::CopyImageToBuffer(Image *, Buffer *)
    {
        DX12_CMD_NOT_IMPLEMENTED("CopyImageToBuffer");
    }
    void Dx12CommandBufferImpl::GenerateMipMaps(Image *)
    {
        DX12_CMD_NOT_IMPLEMENTED("GenerateMipMaps");
    }

    void Dx12CommandBufferImpl::TraceRays(uint32_t, uint32_t, uint32_t)
    {
        DX12_CMD_NOT_IMPLEMENTED("TraceRays");
    }

    void Dx12CommandBufferImpl::BufferBarrier(const BufferBarrierInfo &)
    {
        DX12_CMD_NOT_IMPLEMENTED("BufferBarrier");
    }
    void Dx12CommandBufferImpl::BufferBarriers(const std::vector<BufferBarrierInfo> &)
    {
        DX12_CMD_NOT_IMPLEMENTED("BufferBarriers");
    }
    void Dx12CommandBufferImpl::ImageBarrier(const ImageBarrierInfo &)
    {
        DX12_CMD_NOT_IMPLEMENTED("ImageBarrier");
    }
    void Dx12CommandBufferImpl::ImageBarriers(const std::vector<ImageBarrierInfo> &)
    {
        DX12_CMD_NOT_IMPLEMENTED("ImageBarriers");
    }
    void Dx12CommandBufferImpl::MemoryBarrier(const MemoryBarrierInfo &)
    {
        DX12_CMD_NOT_IMPLEMENTED("MemoryBarrier");
    }
    void Dx12CommandBufferImpl::MemoryBarriers(const std::vector<MemoryBarrierInfo> &)
    {
        DX12_CMD_NOT_IMPLEMENTED("MemoryBarriers");
    }

    void Dx12CommandBufferImpl::SetEvent(Event *, Image *,
                                         PeImageLayout, PeImageLayout,
                                         PeBarrierSync, PeBarrierSync,
                                         PeBarrierAccess, PeBarrierAccess)
    {
        DX12_CMD_NOT_IMPLEMENTED("SetEvent");
    }

    void Dx12CommandBufferImpl::BeginDebugRegion(const std::string &)
    { /* no-op (PIX integration is a later slice) */
    }
    void Dx12CommandBufferImpl::InsertDebugLabel(const std::string &)
    { /* no-op */
    }
    void Dx12CommandBufferImpl::EndDebugRegion()
    { /* no-op */
    }

#undef DX12_CMD_NOT_IMPLEMENTED
} // namespace pe

#endif // PE_WIN32
