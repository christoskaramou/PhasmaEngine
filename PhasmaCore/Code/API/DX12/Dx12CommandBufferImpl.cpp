#include "API/DX12/Dx12CommandBufferImpl.h"

#if defined(PE_WIN32)

#include "API/Buffer.h"
#include "API/DX12/Dx12BufferImpl.h"
#include "API/Debug.h"
#include "API/DX12/Dx12DescriptorHeap.h"
#include "API/DX12/Dx12DescriptorImpl.h"
#include "API/DX12/Dx12ImageImpl.h"
#include "API/DX12/Dx12ImageViewImpl.h"
#include "API/DX12/Dx12PipelineImpl.h"
#include "API/DX12/Dx12RhiImpl.h"
#include "API/DX12/Dx12RootSignature.h"
#include "API/DX12/Dx12Translate.h"
#include "API/Descriptor.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/RHI.h"

namespace pe
{
    using namespace pe_dx12;

    namespace
    {
        bool IsComputePipeline(const Pipeline *pipeline)
        {
            return pipeline && pipeline->GetInfo().pCompShader != nullptr;
        }

        bool IsDepthStencilFormat(::PeFormat fmt)
        {
            return fmt == PE_FORMAT_D32_SFLOAT ||
                   fmt == PE_FORMAT_D24_UNORM_S8_UINT ||
                   fmt == PE_FORMAT_D32_SFLOAT_S8_UINT ||
                   fmt == PE_FORMAT_S8_UINT;
        }

        bool HasStencilComponent(::PeFormat fmt)
        {
            return fmt == PE_FORMAT_D24_UNORM_S8_UINT ||
                   fmt == PE_FORMAT_D32_SFLOAT_S8_UINT ||
                   fmt == PE_FORMAT_S8_UINT;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetAttachmentCpuHandle(Image *image, bool depthStencil, const char *what)
        {
            PE_ERROR_IF(!image, "Dx12CommandBufferImpl::%s: null attachment image", what);
            if (!image->HasRTV())
                image->CreateRTV();

            ImageView *view = image->GetRTV();
            PE_ERROR_IF(!view, "Dx12CommandBufferImpl::%s: image '%s' has no RTV/DSV view",
                        what, image->GetName().c_str());
            const Dx12ImageViewImpl *impl = Dx12ImageViewImpl::From(view);
            PE_ERROR_IF(depthStencil && impl->GetKind() != Dx12ImageViewKind::Dsv,
                        "Dx12CommandBufferImpl::%s: image '%s' needs a DSV view", what, image->GetName().c_str());
            PE_ERROR_IF(!depthStencil && impl->GetKind() != Dx12ImageViewKind::Rtv,
                        "Dx12CommandBufferImpl::%s: image '%s' needs an RTV view", what, image->GetName().c_str());
            return impl->GetCpuHandle();
        }

        bool IndirectRangeFits(size_t bufferSize, size_t offset, uint32_t drawCount, uint32_t stride, uint32_t requiredSize)
        {
            if (offset > bufferSize)
                return false;
            if (drawCount == 0)
                return true;

            const size_t available = bufferSize - offset;
            const size_t commandCount = static_cast<size_t>(drawCount);
            const size_t commandStride = static_cast<size_t>(stride);
            if (commandCount > 1 && commandStride > (std::numeric_limits<size_t>::max() - requiredSize) / (commandCount - 1))
                return false;

            const size_t requiredBytes = (commandCount - 1) * commandStride + requiredSize;
            return requiredBytes <= available;
        }

        void ValidateIndirectDrawState(Pipeline *pipeline, const char *what)
        {
            PE_ERROR_IF(!pipeline, "Dx12CommandBufferImpl::%s: No bound pipeline found!", what);
            PE_ERROR_IF(IsComputePipeline(pipeline), "Dx12CommandBufferImpl::%s: bound pipeline is compute", what);
        }

        bool IsReadOnlyBufferState(D3D12_RESOURCE_STATES state)
        {
            constexpr D3D12_RESOURCE_STATES writeStates =
                D3D12_RESOURCE_STATE_COPY_DEST |
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
                D3D12_RESOURCE_STATE_STREAM_OUT |
                D3D12_RESOURCE_STATE_RENDER_TARGET |
                D3D12_RESOURCE_STATE_DEPTH_WRITE;
            return (state & writeStates) == 0;
        }

        D3D12_RESOURCE_STATES ToD3D12BufferState(PeAccessFlags accessMask)
        {
            if (accessMask & (PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_STORAGE_WRITE))
                return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            if (accessMask & (PE_ACCESS_TRANSFER_WRITE | PE_ACCESS_HOST_WRITE))
                return D3D12_RESOURCE_STATE_COPY_DEST;

            D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
            if (accessMask & PE_ACCESS_TRANSFER_READ)
                state |= D3D12_RESOURCE_STATE_COPY_SOURCE;
            if (accessMask & PE_ACCESS_INDEX_READ)
                state |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
            if (accessMask & (PE_ACCESS_VERTEX_ATTRIBUTE_READ | PE_ACCESS_UNIFORM_READ))
                state |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            if (accessMask & (PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_SAMPLED_READ | PE_ACCESS_SHADER_STORAGE_READ))
                state |= D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            if (accessMask & PE_ACCESS_INDIRECT_COMMAND_READ)
                state |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
            if (state == D3D12_RESOURCE_STATE_COMMON && (accessMask & PE_ACCESS_MEMORY_READ))
                state = D3D12_RESOURCE_STATE_GENERIC_READ;
            return state;
        }

        void PushBufferTransition(std::vector<D3D12_RESOURCE_BARRIER> &batch,
                                  Dx12BufferImpl *buf,
                                  D3D12_RESOURCE_STATES requested)
        {
            if (!buf || !buf->GetResource() || buf->m_heapType != D3D12_HEAP_TYPE_DEFAULT)
                return;

            const D3D12_RESOURCE_STATES before = buf->m_state;
            D3D12_RESOURCE_STATES after = requested;
            if (before != D3D12_RESOURCE_STATE_COMMON &&
                requested != D3D12_RESOURCE_STATE_COMMON &&
                IsReadOnlyBufferState(before) &&
                IsReadOnlyBufferState(requested))
            {
                after = before | requested;
            }

            if (before == after)
                return;

            D3D12_RESOURCE_BARRIER rb{};
            rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            rb.Transition.pResource = buf->GetResource();
            rb.Transition.StateBefore = before;
            rb.Transition.StateAfter = after;
            rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            batch.push_back(rb);

            buf->m_state = after;
        }

        void PushBufferUAV(std::vector<D3D12_RESOURCE_BARRIER> &batch,
                           Dx12BufferImpl *buf)
        {
            if (!buf || !buf->GetResource() || !buf->AllowsUnorderedAccess())
                return;

            D3D12_RESOURCE_BARRIER rb{};
            rb.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            rb.UAV.pResource = buf->GetResource();
            batch.push_back(rb);
        }

        void PushBufferBarrier(std::vector<D3D12_RESOURCE_BARRIER> &batch,
                               const BufferBarrierInfo &info)
        {
            if (!info.buffer)
                return;

            Dx12BufferImpl *buf = Dx12BufferImpl::From(info.buffer);
            const BufferTrackInfo previous = info.buffer->GetTrackInfo();
            const bool previousUavWrite = (previous.accessMask & (PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_STORAGE_WRITE)) != 0;
            if (previousUavWrite && (buf->m_state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
                PushBufferUAV(batch, buf);

            PushBufferTransition(batch, buf, ToD3D12BufferState(info.accessMask));

            BufferTrackInfo &trackInfo = info.buffer->GetTrackInfo();
            trackInfo.stageMask = info.stageMask;
            trackInfo.accessMask = info.accessMask;
            trackInfo.queueFamilyIndex = info.queueFamilyIndex;
        }
    } // namespace

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

        m_heapsBound = false;
        m_lastTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
        BindShaderVisibleHeaps();

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
        m_pendingImageBarrierRegion.clear();
        m_heapsBound = false;
        m_lastTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

#if PE_DEBUG_MODE
        m_owner->m_gpuTimerInfosCount = 0;
        while (!m_owner->m_gpuTimerIdsStack.empty())
            m_owner->m_gpuTimerIdsStack.pop();
        // Cached timers may carry m_inUse=true from an abandoned recording.
        for (auto &info : m_owner->m_gpuTimerInfos)
            if (info.timer)
                info.timer->ResetState();
#endif
    }

    void Dx12CommandBufferImpl::BindShaderVisibleHeaps()
    {
        if (m_heapsBound)
            return;

        Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        if (!rhi)
            return;

        ID3D12DescriptorHeap *heaps[2] = {};
        UINT count = 0;
        if (rhi->GetCbvSrvUavHeap())
            heaps[count++] = rhi->GetCbvSrvUavHeap()->Get();
        if (rhi->GetSamplerHeap())
            heaps[count++] = rhi->GetSamplerHeap()->Get();

        if (count > 0)
            m_cmdList->SetDescriptorHeaps(count, heaps);

        m_heapsBound = true;
    }

    ID3D12CommandSignature *Dx12CommandBufferImpl::GetDrawIndirectSignature(uint32_t stride)
    {
        if (m_drawIndirectSignature && m_drawIndirectSignatureStride == stride)
            return m_drawIndirectSignature.Get();

        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

        D3D12_COMMAND_SIGNATURE_DESC desc{};
        desc.ByteStride = stride;
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs = &argument;

        Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetDevice(), "Dx12CommandBufferImpl::GetDrawIndirectSignature: DX12 device unavailable");
        m_drawIndirectSignature.Reset();
        PE_CHECK(rhi->GetDevice()->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&m_drawIndirectSignature)));
        m_drawIndirectSignatureStride = stride;
        return m_drawIndirectSignature.Get();
    }

    ID3D12CommandSignature *Dx12CommandBufferImpl::GetDrawIndexedIndirectSignature(uint32_t stride)
    {
        if (m_drawIndexedIndirectSignature && m_drawIndexedIndirectSignatureStride == stride)
            return m_drawIndexedIndirectSignature.Get();

        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

        D3D12_COMMAND_SIGNATURE_DESC desc{};
        desc.ByteStride = stride;
        desc.NumArgumentDescs = 1;
        desc.pArgumentDescs = &argument;

        Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetDevice(), "Dx12CommandBufferImpl::GetDrawIndexedIndirectSignature: DX12 device unavailable");
        m_drawIndexedIndirectSignature.Reset();
        PE_CHECK(rhi->GetDevice()->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&m_drawIndexedIndirectSignature)));
        m_drawIndexedIndirectSignatureStride = stride;
        return m_drawIndexedIndirectSignature.Get();
    }

    void Dx12CommandBufferImpl::FlushBarriers()
    {
        if (m_barrierBatch.empty())
            return;

        const bool markImageBarrier = !m_pendingImageBarrierRegion.empty();
        if (markImageBarrier)
            BeginDebugRegion(m_pendingImageBarrierRegion);

        m_cmdList->ResourceBarrier(static_cast<UINT>(m_barrierBatch.size()), m_barrierBatch.data());

        if (markImageBarrier)
            EndDebugRegion();

        m_barrierBatch.clear();
        m_pendingImageBarrierRegion.clear();
    }

    void Dx12CommandBufferImpl::MarkPendingImageBarrierRegion(const char *name)
    {
        if (m_pendingImageBarrierRegion.empty() || std::string{name} == "ImageGroupBarrier")
            m_pendingImageBarrierRegion = name;
    }

    // ----------------------------------------------------------------------
    // Surfaces still gated by DX12_CMD_CARVE_OUT are audited-unreachable on
    // DX12 today: TraceRays (RT explicitly out of Phase 1 — caps.rayTracing
    // == false), PushDescriptor (zero callers tree-wide), SetEvent (only
    // reachable via the Lua CommandBindings::SetEvent shim, no script in
    // the repo invokes it). Macro fires PE_ERROR if a future caller hits
    // them so we notice instead of silently no-oping.
    // ----------------------------------------------------------------------

#define DX12_CMD_CARVE_OUT(name) PE_ERROR("Dx12CommandBufferImpl::" name " is a DX12 carve-out (unreachable today; audited 2026-05-06)")

    void Dx12CommandBufferImpl::BlitImage(Image *src, Image *dst, const ImageBlit &region, PeFilter filter)
    {
        PE_ERROR_IF(!dst, "Dx12CommandBufferImpl::BlitImage: null destination image");
        dst->Blit(m_owner, src, region, filter);
    }
    void Dx12CommandBufferImpl::ClearColors(std::vector<Image *> images)
    {
        // DX12 ClearRenderTargetView requires the resource in RENDER_TARGET state, so
        // we route through the engine's barrier tracker with the attachment layout
        // (PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL maps to D3D12_RESOURCE_STATE_RENDER_TARGET).
        // The Vulkan path uses TRANSFER_DST_OPTIMAL because vkCmdClearColorImage is a
        // transfer command - different verb, same intent.
        std::vector<ImageBarrierInfo> barriers(images.size());
        for (size_t i = 0; i < images.size(); ++i)
        {
            PE_ERROR_IF(!images[i], "Dx12CommandBufferImpl::ClearColors: image %zu is null", i);
            PE_ERROR_IF(IsDepthStencilFormat(images[i]->GetFormat()),
                        "Dx12CommandBufferImpl::ClearColors: image '%s' is depth/stencil",
                        images[i]->GetName().c_str());
            barriers[i].image = images[i];
            barriers[i].layout = PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
            barriers[i].stageFlags = PE_STAGE_COLOR_ATTACHMENT_OUTPUT;
            barriers[i].accessMask = PE_ACCESS_COLOR_ATTACHMENT_WRITE;
        }
        Image::Barriers(m_owner, barriers);
        FlushBarriers();

        for (Image *image : images)
        {
            const vec4 &c = image->m_clearColor;
            const float color[4] = {c[0], c[1], c[2], c[3]};
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetAttachmentCpuHandle(image, false, "ClearColors");
            m_cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);
        }
    }

    void Dx12CommandBufferImpl::ClearDepthStencils(std::vector<Image *> images)
    {
        std::vector<ImageBarrierInfo> barriers(images.size());
        for (size_t i = 0; i < images.size(); ++i)
        {
            PE_ERROR_IF(!images[i], "Dx12CommandBufferImpl::ClearDepthStencils: image %zu is null", i);
            PE_ERROR_IF(!IsDepthStencilFormat(images[i]->GetFormat()),
                        "Dx12CommandBufferImpl::ClearDepthStencils: image '%s' is not depth/stencil",
                        images[i]->GetName().c_str());
            barriers[i].image = images[i];
            barriers[i].layout = PE_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            barriers[i].stageFlags = PE_STAGE_EARLY_FRAGMENT_TESTS | PE_STAGE_LATE_FRAGMENT_TESTS;
            barriers[i].accessMask = PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE;
        }
        Image::Barriers(m_owner, barriers);
        FlushBarriers();

        for (Image *image : images)
        {
            const float depth = image->m_clearColor[0];
            const uint8_t stencil = static_cast<uint8_t>(image->m_clearColor[1]);
            D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
            if (HasStencilComponent(image->GetFormat()))
                flags |= D3D12_CLEAR_FLAG_STENCIL;
            const D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetAttachmentCpuHandle(image, true, "ClearDepthStencils");
            m_cmdList->ClearDepthStencilView(dsv, flags, depth, stencil, 0, nullptr);
        }
    }

    void Dx12CommandBufferImpl::BeginPass(uint32_t count, Attachment *attachments, const std::string &name, bool /*skipDynamicPass*/)
    {
        PE_ERROR_IF(count > 0 && !attachments, "Dx12CommandBufferImpl::BeginPass: null attachments");

        // DX12 has no separate "render pass / framebuffer" object in the legacy
        // command-list path; we record clears + OMSetRenderTargets directly. The
        // skipDynamicPass flag is a Vulkan-only switch (force vk::beginRenderPass2
        // over beginRendering) and has no analog here, so it is intentionally
        // ignored. The engine still consults m_dynamicPass elsewhere; set it to
        // true so DX12 follows the dynamic-rendering control flow.
        BeginDebugRegion(name + "_pass");

        m_owner->m_dynamicPass = true;
        m_owner->m_attachmentCount = count;
        m_owner->m_attachments = attachments;

        std::vector<ImageBarrierInfo> attachmentBarriers;
        attachmentBarriers.reserve(count);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        uint32_t rtvCount = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
        bool hasDsv = false;

        for (uint32_t i = 0; i < count; ++i)
        {
            const Attachment &att = attachments[i];
            PE_ERROR_IF(!att.image, "Dx12CommandBufferImpl::BeginPass: attachment %u has null image", i);

            const ::PeFormat fmt = att.image->GetFormat();
            const bool isDepthStencil = IsDepthStencilFormat(fmt);

            ImageBarrierInfo barrier{};
            barrier.image = att.image;

            if (isDepthStencil)
            {
                PE_ERROR_IF(hasDsv, "Dx12CommandBufferImpl::BeginPass: more than one depth/stencil attachment");
                barrier.layout = PE_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                barrier.stageFlags = PE_STAGE_EARLY_FRAGMENT_TESTS | PE_STAGE_LATE_FRAGMENT_TESTS;
                barrier.accessMask = (att.loadOp == PE_LOAD_OP_LOAD)
                                         ? PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ
                                         : PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE;
                dsvHandle = GetAttachmentCpuHandle(att.image, true, "BeginPass");
                hasDsv = true;
            }
            else
            {
                PE_ERROR_IF(rtvCount >= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT,
                            "Dx12CommandBufferImpl::BeginPass: more than %u color attachments",
                            D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
                barrier.layout = PE_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
                barrier.stageFlags = PE_STAGE_COLOR_ATTACHMENT_OUTPUT;
                barrier.accessMask = (att.loadOp == PE_LOAD_OP_LOAD)
                                         ? PE_ACCESS_COLOR_ATTACHMENT_READ
                                         : PE_ACCESS_COLOR_ATTACHMENT_WRITE;
                rtvHandles[rtvCount++] = GetAttachmentCpuHandle(att.image, false, "BeginPass");
            }

            attachmentBarriers.push_back(barrier);
        }

        Image::Barriers(m_owner, attachmentBarriers);
        FlushBarriers();

        m_cmdList->OMSetRenderTargets(rtvCount,
                                      rtvCount > 0 ? rtvHandles : nullptr,
                                      FALSE,
                                      hasDsv ? &dsvHandle : nullptr);

        for (uint32_t i = 0; i < count; ++i)
        {
            const Attachment &att = attachments[i];
            const ::PeFormat fmt = att.image->GetFormat();

            if (IsDepthStencilFormat(fmt))
            {
                D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(0);
                if (att.loadOp == PE_LOAD_OP_CLEAR)
                    flags |= D3D12_CLEAR_FLAG_DEPTH;
                if (HasStencilComponent(fmt) && att.stencilLoadOp == PE_LOAD_OP_CLEAR)
                    flags |= D3D12_CLEAR_FLAG_STENCIL;
                if (flags == 0)
                    continue;

                const float depth = att.image->m_clearColor[0];
                const uint8_t stencil = static_cast<uint8_t>(att.image->m_clearColor[1]);
                m_cmdList->ClearDepthStencilView(GetAttachmentCpuHandle(att.image, true, "BeginPass"),
                                                 flags, depth, stencil, 0, nullptr);
            }
            else if (att.loadOp == PE_LOAD_OP_CLEAR)
            {
                const vec4 &c = att.image->m_clearColor;
                const float color[4] = {c[0], c[1], c[2], c[3]};
                m_cmdList->ClearRenderTargetView(GetAttachmentCpuHandle(att.image, false, "BeginPass"),
                                                 color, 0, nullptr);
            }
        }
    }

    void Dx12CommandBufferImpl::EndPass()
    {
        // Mirror the Vulkan EndPass tracker fix-up: attachments entered the pass with
        // an *_ATTACHMENT_READ mask (LOAD_OP_LOAD) but the pass executes writes, so the
        // final access mask must be *_ATTACHMENT_WRITE before the next barrier query.
        for (uint32_t i = 0; i < m_owner->m_attachmentCount; ++i)
        {
            const Attachment &att = m_owner->m_attachments[i];
            if (att.loadOp == PE_LOAD_OP_LOAD && att.image)
            {
                const ::PeFormat fmt = att.image->GetFormat();
                att.image->m_trackInfos[0][0].accessMask = IsDepthStencilFormat(fmt)
                                                               ? PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE
                                                               : PE_ACCESS_COLOR_ATTACHMENT_WRITE;
            }
        }

        // DX12 has no end-render-pass call in the legacy command-list path; just
        // unbind so the next non-pass command (e.g. a copy) doesn't trip over a
        // stale RTV/DSV binding.
        m_cmdList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);

        EndDebugRegion();

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
    }

    void Dx12CommandBufferImpl::BindPipeline(PassInfo &passInfo, bool bindDescriptors)
    {
        Pipeline *pipeline = CommandBuffer::GetPipeline(m_owner->m_renderPass, passInfo);
        PE_ERROR_IF(!pipeline, "Dx12CommandBufferImpl::BindPipeline: pipeline creation failed");

        if (pipeline != m_owner->m_boundPipeline)
        {
            m_owner->m_boundPipeline = pipeline;

            Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
            PE_ERROR_IF(!rhi || !rhi->GetSharedRootSig(), "Dx12CommandBufferImpl::BindPipeline: shared root signature unavailable");

            ID3D12RootSignature *rootSig = rhi->GetSharedRootSig()->Get();
            ID3D12PipelineState *pso = GetDx12Pipeline(pipeline);
            PE_ERROR_IF(!pso, "Dx12CommandBufferImpl::BindPipeline: pipeline has no PSO");

            BindShaderVisibleHeaps();
            m_cmdList->SetPipelineState(pso);

            if (IsComputePipeline(pipeline))
            {
                m_cmdList->SetComputeRootSignature(rootSig);
            }
            else
            {
                m_cmdList->SetGraphicsRootSignature(rootSig);
                const D3D12_PRIMITIVE_TOPOLOGY topology = Topology(passInfo.topology);
                if (topology != m_lastTopology)
                {
                    m_cmdList->IASetPrimitiveTopology(topology);
                    m_lastTopology = topology;
                }
            }
        }

        if (bindDescriptors)
        {
            const auto &descriptors = passInfo.GetDescriptors(RHII.GetFrameIndex());
            const uint32_t count = static_cast<uint32_t>(descriptors.size());
            BindDescriptors(count, descriptors.data());
        }
    }

    void Dx12CommandBufferImpl::BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding, uint32_t bindingCount)
    {
        PE_ERROR_IF(bindingCount == 0, "Dx12CommandBufferImpl::BindVertexBuffer: bindingCount is zero");
        if (m_owner->m_boundVertexBuffer == buffer &&
            m_owner->m_boundVertexBufferOffset == offset &&
            m_owner->m_boundVertexBufferFirstBinding == firstBinding &&
            m_owner->m_boundVertexBufferBindingCount == bindingCount)
            return;

        PE_ERROR_IF(!buffer, "Dx12CommandBufferImpl::BindVertexBuffer: null buffer");
        PE_ERROR_IF(!m_owner->m_boundPipeline, "Dx12CommandBufferImpl::BindVertexBuffer: no bound pipeline (DX12 needs stride from PSO reflection)");
        PE_ERROR_IF(offset > buffer->Size(), "Dx12CommandBufferImpl::BindVertexBuffer: offset exceeds buffer size");

        PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(buffer), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

        m_owner->m_boundVertexBuffer = buffer;
        m_owner->m_boundVertexBufferOffset = offset;
        m_owner->m_boundVertexBufferFirstBinding = firstBinding;
        m_owner->m_boundVertexBufferBindingCount = bindingCount;

        const Dx12PipelineImpl *pipelineImpl = Dx12PipelineImpl::From(m_owner->m_boundPipeline);
        const Dx12BufferImpl *bufImpl = Dx12BufferImpl::From(buffer);

        std::vector<D3D12_VERTEX_BUFFER_VIEW> views(bindingCount);
        for (uint32_t i = 0; i < bindingCount; ++i)
        {
            const uint32_t binding = firstBinding + i;
            D3D12_VERTEX_BUFFER_VIEW &view = views[i];
            view.BufferLocation = bufImpl->GetResource()->GetGPUVirtualAddress() + offset;
            view.SizeInBytes = static_cast<UINT>(buffer->Size() - offset);
            view.StrideInBytes = pipelineImpl->GetVertexBindingStride(binding);
            PE_ERROR_IF(view.StrideInBytes == 0,
                        "Dx12CommandBufferImpl::BindVertexBuffer: stride for binding %u is zero (pipeline reflection produced no input)",
                        binding);
        }

        m_cmdList->IASetVertexBuffers(firstBinding, bindingCount, views.data());
    }

    void Dx12CommandBufferImpl::BindIndexBuffer(Buffer *buffer, size_t offset)
    {
        if (m_owner->m_boundIndexBuffer == buffer && m_owner->m_boundIndexBufferOffset == offset)
            return;

        PE_ERROR_IF(!buffer, "Dx12CommandBufferImpl::BindIndexBuffer: null buffer");
        PE_ERROR_IF(offset > buffer->Size(), "Dx12CommandBufferImpl::BindIndexBuffer: offset exceeds buffer size");

        PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(buffer), D3D12_RESOURCE_STATE_INDEX_BUFFER);

        m_owner->m_boundIndexBuffer = buffer;
        m_owner->m_boundIndexBufferOffset = offset;

        const Dx12BufferImpl *bufImpl = Dx12BufferImpl::From(buffer);

        D3D12_INDEX_BUFFER_VIEW view{};
        view.BufferLocation = bufImpl->GetResource()->GetGPUVirtualAddress() + offset;
        view.SizeInBytes = static_cast<UINT>(buffer->Size() - offset);
        view.Format = DXGI_FORMAT_R32_UINT;

        m_cmdList->IASetIndexBuffer(&view);
    }

    void Dx12CommandBufferImpl::BindDescriptors(uint32_t count, Descriptor *const *descriptors)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "Dx12CommandBufferImpl::BindDescriptors: No bound pipeline found!");
        PE_ERROR_IF(count > 0 && !descriptors, "Dx12CommandBufferImpl::BindDescriptors: null descriptor array");

        BindShaderVisibleHeaps();

        const bool compute = IsComputePipeline(m_owner->m_boundPipeline);

        for (uint32_t i = 0; i < count; ++i)
        {
            Descriptor *descriptor = descriptors[i];
            PE_ERROR_IF(!descriptor, "Dx12CommandBufferImpl::BindDescriptors: descriptor %u is null", i);
            const Dx12DescriptorImpl *impl = Dx12DescriptorImpl::From(descriptor);

            // A single Descriptor may carry tables for one or more dxSpaces; bind
            // both CBV/SRV/UAV and sampler tables for every space it reports. The
            // table accessors return a zero-pointer handle when the descriptor
            // does not occupy that space, so the inner check stays cheap.
            for (uint32_t space = 0; space < DX12_DESCRIPTOR_SPACE_COUNT; ++space)
            {
                const D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavTable = impl->GetCbvSrvUavTableGpuHandle(space);
                if (cbvSrvUavTable.ptr != 0)
                {
                    const uint32_t rootIdx = Dx12CbvSrvUavRootIndex(space);
                    if (compute)
                        m_cmdList->SetComputeRootDescriptorTable(rootIdx, cbvSrvUavTable);
                    else
                        m_cmdList->SetGraphicsRootDescriptorTable(rootIdx, cbvSrvUavTable);
                }

                const D3D12_GPU_DESCRIPTOR_HANDLE samplerTable = impl->GetSamplerTableGpuHandle(space);
                if (samplerTable.ptr != 0)
                {
                    const uint32_t rootIdx = Dx12SamplerRootIndex(space);
                    if (compute)
                        m_cmdList->SetComputeRootDescriptorTable(rootIdx, samplerTable);
                    else
                        m_cmdList->SetGraphicsRootDescriptorTable(rootIdx, samplerTable);
                }
            }
        }
    }
    void Dx12CommandBufferImpl::PushDescriptor(uint32_t, const std::vector<PushDescriptorInfo> &)
    {
        DX12_CMD_CARVE_OUT("PushDescriptor");
    }

    void Dx12CommandBufferImpl::SetViewport(float x, float y, float width, float height)
    {
        const auto *rhi = static_cast<const Dx12RhiImpl *>(RHII.GetImpl());
        const bool flipY = rhi && rhi->SupportsInvertedViewportHeightFlipsY();

        D3D12_VIEWPORT vp{};
        vp.TopLeftX = x;
        vp.TopLeftY = flipY ? y + height : y;
        vp.Width = width;
        vp.Height = flipY ? -height : height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        m_cmdList->RSSetViewports(1, &vp);
    }
    void Dx12CommandBufferImpl::SetScissor(int x, int y, uint32_t width, uint32_t height)
    {
        D3D12_RECT rect{};
        rect.left = x;
        rect.top = y;
        rect.right = x + static_cast<LONG>(width);
        rect.bottom = y + static_cast<LONG>(height);
        m_cmdList->RSSetScissorRects(1, &rect);
    }
    void Dx12CommandBufferImpl::SetLineWidth(float)
    {
        // DX12 has no concept of dynamic line width; lines are 1px and the
        // raster state is baked into the PSO. The Vulkan path keeps it dynamic,
        // so this surface stays callable but is a no-op on DX12.
    }
    void Dx12CommandBufferImpl::SetDepthBias(float, float, float)
    {
        // DX12 bakes depth bias into the PSO rasterizer state. Until the
        // pipeline-binding slice rebuilds PSO variants on demand, we accept the
        // call but rely on the bound PSO to provide the configured values.
    }
    void Dx12CommandBufferImpl::SetDepthTestEnable(uint32_t)
    {
        // DX12 bakes depth test enable into the PSO depth-stencil state.
    }
    void Dx12CommandBufferImpl::SetDepthWriteEnable(uint32_t)
    {
        // DX12 bakes depth write enable into the PSO depth-stencil state.
    }

    void Dx12CommandBufferImpl::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "Dx12CommandBufferImpl::Dispatch: No bound pipeline found!");
        PE_ERROR_IF(!IsComputePipeline(m_owner->m_boundPipeline), "Dx12CommandBufferImpl::Dispatch: bound pipeline is not compute");
        FlushBarriers();
        m_cmdList->Dispatch(groupCountX, groupCountY, groupCountZ);
    }
    void Dx12CommandBufferImpl::PushConstants(const PushConstantsBlock<128> &constants)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "Dx12CommandBufferImpl::PushConstants: No bound pipeline found!");

        const auto &sizes = m_owner->m_boundPipeline->GetInfo().m_pushConstantSizes;
        if (sizes.empty() || sizes[0] == 0)
            return;

        // Dx12PipelineImpl collapses vert+frag push ranges into a single 0-based
        // entry sized to the larger of the two. Root constants live at b0/space0
        // (DX12_ROOT_CONSTANTS_INDEX) per the shared root signature layout.
        const uint32_t numDwords = (sizes[0] + 3u) / 4u;
        PE_ERROR_IF(numDwords > 32, "Dx12CommandBufferImpl::PushConstants: push constants exceed shared root constant budget");
        const bool compute = IsComputePipeline(m_owner->m_boundPipeline);

        if (compute)
            m_cmdList->SetComputeRoot32BitConstants(DX12_ROOT_CONSTANTS_INDEX, numDwords, constants.Data(), 0);
        else
            m_cmdList->SetGraphicsRoot32BitConstants(DX12_ROOT_CONSTANTS_INDEX, numDwords, constants.Data(), 0);
    }

    void Dx12CommandBufferImpl::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "Dx12CommandBufferImpl::Draw: No bound pipeline found!");
        PE_ERROR_IF(IsComputePipeline(m_owner->m_boundPipeline), "Dx12CommandBufferImpl::Draw: bound pipeline is compute");
        FlushBarriers();
        m_cmdList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
    }
    void Dx12CommandBufferImpl::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "Dx12CommandBufferImpl::DrawIndexed: No bound pipeline found!");
        PE_ERROR_IF(IsComputePipeline(m_owner->m_boundPipeline), "Dx12CommandBufferImpl::DrawIndexed: bound pipeline is compute");
        FlushBarriers();
        m_cmdList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }
    void Dx12CommandBufferImpl::DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        static_assert(PE_DRAW_INDIRECT_COMMAND_SIZE == sizeof(D3D12_DRAW_ARGUMENTS));
        ValidateIndirectDrawState(m_owner->m_boundPipeline, "DrawIndirect");
        PE_ERROR_IF(!indirectBuffer, "Dx12CommandBufferImpl::DrawIndirect: null indirect buffer");
        PE_ERROR_IF(stride < PE_DRAW_INDIRECT_COMMAND_SIZE,
                    "Dx12CommandBufferImpl::DrawIndirect: stride %u is smaller than D3D12_DRAW_ARGUMENTS (%u)",
                    stride, PE_DRAW_INDIRECT_COMMAND_SIZE);
        PE_ERROR_IF(!IndirectRangeFits(indirectBuffer->Size(), offset, drawCount, stride, PE_DRAW_INDIRECT_COMMAND_SIZE),
                    "Dx12CommandBufferImpl::DrawIndirect: argument range exceeds buffer size");
        if (drawCount == 0)
            return;

        PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(indirectBuffer), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        FlushBarriers();
        m_cmdList->ExecuteIndirect(GetDrawIndirectSignature(stride),
                                   drawCount,
                                   Dx12BufferImpl::From(indirectBuffer)->GetResource(),
                                   static_cast<UINT64>(offset),
                                   nullptr,
                                   0);
    }
    void Dx12CommandBufferImpl::DrawIndexedIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        static_assert(PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE == sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
        ValidateIndirectDrawState(m_owner->m_boundPipeline, "DrawIndexedIndirect");
        PE_ERROR_IF(!indirectBuffer, "Dx12CommandBufferImpl::DrawIndexedIndirect: null indirect buffer");
        PE_ERROR_IF(stride < PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
                    "Dx12CommandBufferImpl::DrawIndexedIndirect: stride %u is smaller than D3D12_DRAW_INDEXED_ARGUMENTS (%u)",
                    stride, PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
        PE_ERROR_IF(!IndirectRangeFits(indirectBuffer->Size(), offset, drawCount, stride, PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE),
                    "Dx12CommandBufferImpl::DrawIndexedIndirect: argument range exceeds buffer size");
        if (drawCount == 0)
            return;

        PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(indirectBuffer), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        FlushBarriers();
        m_cmdList->ExecuteIndirect(GetDrawIndexedIndirectSignature(stride),
                                   drawCount,
                                   Dx12BufferImpl::From(indirectBuffer)->GetResource(),
                                   static_cast<UINT64>(offset),
                                   nullptr,
                                   0);
    }
    void Dx12CommandBufferImpl::DrawIndexedIndirectCount(Buffer *indirectBuffer, size_t offset, Buffer *countBuffer, size_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
    {
        static_assert(PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE == sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
        ValidateIndirectDrawState(m_owner->m_boundPipeline, "DrawIndexedIndirectCount");
        PE_ERROR_IF(!indirectBuffer, "Dx12CommandBufferImpl::DrawIndexedIndirectCount: null indirect buffer");
        PE_ERROR_IF(!countBuffer, "Dx12CommandBufferImpl::DrawIndexedIndirectCount: null count buffer");
        PE_ERROR_IF(stride < PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE,
                    "Dx12CommandBufferImpl::DrawIndexedIndirectCount: stride %u is smaller than D3D12_DRAW_INDEXED_ARGUMENTS (%u)",
                    stride, PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
        PE_ERROR_IF(!IndirectRangeFits(indirectBuffer->Size(), offset, maxDrawCount, stride, PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE),
                    "Dx12CommandBufferImpl::DrawIndexedIndirectCount: argument range exceeds buffer size");
        PE_ERROR_IF(countBufferOffset > countBuffer->Size() || sizeof(uint32_t) > countBuffer->Size() - countBufferOffset,
                    "Dx12CommandBufferImpl::DrawIndexedIndirectCount: count range exceeds buffer size");
        if (maxDrawCount == 0)
            return;

        PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(indirectBuffer), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(countBuffer), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        FlushBarriers();
        m_cmdList->ExecuteIndirect(GetDrawIndexedIndirectSignature(stride),
                                   maxDrawCount,
                                   Dx12BufferImpl::From(indirectBuffer)->GetResource(),
                                   static_cast<UINT64>(offset),
                                   Dx12BufferImpl::From(countBuffer)->GetResource(),
                                   static_cast<UINT64>(countBufferOffset));
    }

    void Dx12CommandBufferImpl::FillBuffer(Buffer *buffer, size_t offset, size_t size, uint32_t data)
    {
        PE_ERROR_IF(!buffer, "Dx12CommandBufferImpl::FillBuffer: null buffer");
        PE_ERROR_IF(offset + size > buffer->Size(), "Dx12CommandBufferImpl::FillBuffer: range overflow");
        if (!size)
            return;

        std::vector<uint32_t> words((size + sizeof(uint32_t) - 1) / sizeof(uint32_t), data);
        CopyBufferStaged(buffer, words.data(), size, offset);

        BufferTrackInfo &trackInfo = buffer->GetTrackInfo();
        trackInfo.stageMask = PE_STAGE_CLEAR;
        trackInfo.accessMask = PE_ACCESS_TRANSFER_WRITE;
    }

    void Dx12CommandBufferImpl::CopyBuffer(Buffer *src, Buffer *dst, size_t size, size_t srcOffset, size_t dstOffset)
    {
        PE_ERROR_IF(!src || !dst, "Dx12CommandBufferImpl::CopyBuffer: null buffer");
        PE_ERROR_IF(srcOffset + size > src->Size(), "Dx12CommandBufferImpl::CopyBuffer: source range overflow");
        PE_ERROR_IF(dstOffset + size > dst->Size(), "Dx12CommandBufferImpl::CopyBuffer: destination range overflow");
        if (!size)
            return;

        PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(src), D3D12_RESOURCE_STATE_COPY_SOURCE);
        PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(dst), D3D12_RESOURCE_STATE_COPY_DEST);
        FlushBarriers();
        m_cmdList->CopyBufferRegion(Dx12BufferImpl::From(dst)->GetResource(),
                                    static_cast<UINT64>(dstOffset),
                                    Dx12BufferImpl::From(src)->GetResource(),
                                    static_cast<UINT64>(srcOffset),
                                    static_cast<UINT64>(size));

        BufferTrackInfo &trackInfo = dst->GetTrackInfo();
        trackInfo.stageMask = PE_STAGE_TRANSFER;
        trackInfo.accessMask = PE_ACCESS_TRANSFER_WRITE;
    }

    void Dx12CommandBufferImpl::CopyBufferStaged(Buffer *buffer, void *data, size_t size, size_t dstOffset)
    {
        PE_ERROR_IF(!buffer, "Dx12CommandBufferImpl::CopyBufferStaged: null buffer");
        buffer->CopyBufferStaged(m_owner, data, size, dstOffset);
    }

    void Dx12CommandBufferImpl::CopyDataToImageStaged(Image *image, void *data, size_t size, uint32_t baseArrayLayer, uint32_t layerCount, uint32_t mipLevel)
    {
        PE_ERROR_IF(!image, "Dx12CommandBufferImpl::CopyDataToImageStaged: null image");
        image->CopyDataToImageStaged(m_owner, data, size, baseArrayLayer, layerCount, mipLevel);
    }

    void Dx12CommandBufferImpl::CopyImage(Image *src, Image *dst)
    {
        PE_ERROR_IF(!src || !dst, "Dx12CommandBufferImpl::CopyImage: null image");
        dst->CopyImage(m_owner, src);
    }

    void Dx12CommandBufferImpl::CopyImageToBuffer(Image *src, Buffer *dst)
    {
        PE_ERROR_IF(!src || !dst, "Dx12CommandBufferImpl::CopyImageToBuffer: null resource");
        src->CopyToBuffer(m_owner, dst);
    }
    void Dx12CommandBufferImpl::GenerateMipMaps(Image *image)
    {
        PE_ERROR_IF(!image, "Dx12CommandBufferImpl::GenerateMipMaps: null image");
        image->GenerateMipMaps(m_owner);
    }

    void Dx12CommandBufferImpl::TraceRays(uint32_t, uint32_t, uint32_t)
    {
        DX12_CMD_CARVE_OUT("TraceRays");
    }

    namespace
    {
        // Push a transition barrier for a single image into the batch.
        // No-op when the source and destination states already match; DX12
        // rejects no-op transitions with a debug-layer error.
        bool PushImageTransition(std::vector<D3D12_RESOURCE_BARRIER> &batch,
                                 Dx12ImageImpl *img,
                                 PeImageLayout newLayout)
        {
            const D3D12_RESOURCE_STATES before = img->m_state;
            const D3D12_RESOURCE_STATES after = pe_dx12::ToD3D12ResourceState(newLayout);
            if (before == after)
                return false;

            D3D12_RESOURCE_BARRIER rb{};
            rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            rb.Transition.pResource = img->GetResource();
            rb.Transition.StateBefore = before;
            rb.Transition.StateAfter = after;
            rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            batch.push_back(rb);

            img->m_state = after;
            return true;
        }

    } // namespace

    void Dx12CommandBufferImpl::BufferBarrier(const BufferBarrierInfo &info)
    {
        PushBufferBarrier(m_barrierBatch, info);
    }
    void Dx12CommandBufferImpl::BufferBarriers(const std::vector<BufferBarrierInfo> &infos)
    {
        m_barrierBatch.reserve(m_barrierBatch.size() + infos.size());
        for (const auto &info : infos)
            PushBufferBarrier(m_barrierBatch, info);
    }
    void Dx12CommandBufferImpl::ImageBarrier(const ImageBarrierInfo &info)
    {
        if (!info.image)
            return;
        if (PushImageTransition(m_barrierBatch, Dx12ImageImpl::From(info.image), info.layout))
            MarkPendingImageBarrierRegion("ImageBarrier");
    }
    void Dx12CommandBufferImpl::ImageBarriers(const std::vector<ImageBarrierInfo> &infos)
    {
        m_barrierBatch.reserve(m_barrierBatch.size() + infos.size());
        for (const auto &info : infos)
        {
            if (!info.image)
                continue;
            if (PushImageTransition(m_barrierBatch, Dx12ImageImpl::From(info.image), info.layout))
                MarkPendingImageBarrierRegion("ImageGroupBarrier");
        }
    }
    void Dx12CommandBufferImpl::MemoryBarrier(const MemoryBarrierInfo &)
    {
        // Global UAV barrier: pResource = nullptr instructs the GPU to flush
        // every preceding UAV write before any subsequent UAV read/write.
        // Stage/access masks are ignored in the legacy model; they will be
        // honored once the enhanced-barrier (D3D12_BARRIER) path is wired.
        D3D12_RESOURCE_BARRIER rb{};
        rb.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rb.UAV.pResource = nullptr;
        m_barrierBatch.push_back(rb);
    }
    void Dx12CommandBufferImpl::MemoryBarriers(const std::vector<MemoryBarrierInfo> &infos)
    {
        if (infos.empty())
            return;
        // One global UAV barrier flushes everything regardless of how many
        // logical Vulkan-style memory barriers were requested.
        D3D12_RESOURCE_BARRIER rb{};
        rb.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        rb.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rb.UAV.pResource = nullptr;
        m_barrierBatch.push_back(rb);
    }

    void Dx12CommandBufferImpl::SetEvent(Event *, Image *,
                                         PeImageLayout, PeImageLayout,
                                         PeBarrierSync, PeBarrierSync,
                                         PeBarrierAccess, PeBarrierAccess)
    {
        DX12_CMD_CARVE_OUT("SetEvent");
    }

    void Dx12CommandBufferImpl::BeginDebugRegion(const std::string &name)
    {
        // PIX label integration is a later slice; the call still drives the
        // GpuTimer Start that ProfilerWidget/profiler_snapshot read.
        Debug::BeginCmdRegion(m_owner, name);
    }
    void Dx12CommandBufferImpl::InsertDebugLabel(const std::string &name)
    {
        Debug::InsertCmdLabel(m_owner, name);
    }
    void Dx12CommandBufferImpl::EndDebugRegion()
    {
        Debug::EndCmdRegion(m_owner);
    }

#undef DX12_CMD_CARVE_OUT
} // namespace pe

#endif // PE_WIN32
