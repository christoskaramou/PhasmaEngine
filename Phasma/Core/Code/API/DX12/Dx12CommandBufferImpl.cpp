#include "API/DX12/Dx12CommandBufferImpl.h"

#if defined(PE_WIN32)

#include "API/Buffer.h"
#include "API/DX12/Dx12BufferImpl.h"
#include "API/DX12/Dx12DescriptorHeap.h"
#include "API/DX12/Dx12DescriptorImpl.h"
#include "API/DX12/Dx12ImageImpl.h"
#include "API/DX12/Dx12ImageViewImpl.h"
#include "API/DX12/Dx12PipelineImpl.h"
#include "API/DX12/Dx12QueryPool.h"
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
            return pipeline && pipeline->GetType() == Pipeline::Type::Compute;
        }

        bool IsRayTracingPipeline(const Pipeline *pipeline)
        {
            return pipeline && pipeline->GetType() == Pipeline::Type::RayTracing;
        }

        bool UsesComputeRootSignature(const Pipeline *pipeline)
        {
            return IsComputePipeline(pipeline) || IsRayTracingPipeline(pipeline);
        }

        bool IsGraphicsPipeline(const Pipeline *pipeline)
        {
            return pipeline && !IsComputePipeline(pipeline) && !IsRayTracingPipeline(pipeline);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE GetAttachmentCpuHandle(Image *image, ImageView *attachmentView, bool depthStencil, const char *what)
        {
            PE_ERROR_IF(!image, "Dx12CommandBufferImpl::%s: null attachment image", what);
            if (!attachmentView && !image->HasRTV())
                image->CreateRTV();

            ImageView *view = attachmentView ? attachmentView : image->GetRTV();
            PE_ERROR_IF(!view, "Dx12CommandBufferImpl::%s: image '%s' has no RTV/DSV view",
                        what, image->GetName().c_str());
            PE_ERROR_IF(view->GetParent() != image,
                        "Dx12CommandBufferImpl::%s: attachment view does not belong to image '%s'",
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

        void ValidateIndirectDrawState(Pipeline *pipeline, bool externalRenderPipelineBound, const char *what)
        {
            if (pipeline)
            {
                PE_ERROR_IF(!IsGraphicsPipeline(pipeline), "Dx12CommandBufferImpl::%s: bound pipeline is not graphics", what);
                return;
            }

            PE_ERROR_IF(!externalRenderPipelineBound, "Dx12CommandBufferImpl::%s: No bound pipeline found!", what);
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

        D3D12_RESOURCE_STATES ToD3D12BufferState(const BufferBarrierInfo &info)
        {
            const PeAccessFlags accessMask = info.accessMask;
            const bool asAccess = (accessMask & (PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR |
                                                 PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR)) != 0;
            if (asAccess && info.buffer)
            {
                const PeBufferUsageFlags usage = info.buffer->Usage();
                if (usage & PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_KHR)
                    return D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
                if (usage & PE_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_KHR)
                    return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
            }
            if (accessMask & PE_ACCESS_ACCELERATION_STRUCTURE_READ_KHR)
                return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;

            return ToD3D12BufferState(accessMask);
        }

        D3D12_RESOURCE_STATES ToD3D12ImageState(const ImageBarrierInfo &info)
        {
            if (info.layout == PE_IMAGE_LAYOUT_GENERAL)
            {
                const bool storageAccess =
                    (info.accessMask &
                     (PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE | PE_ACCESS_SHADER_WRITE | PE_ACCESS_MEMORY_WRITE)) != 0;
                if (storageAccess)
                    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

                const bool shaderRead =
                    (info.accessMask & (PE_ACCESS_SHADER_READ | PE_ACCESS_SHADER_SAMPLED_READ)) != 0;
                if (shaderRead)
                    return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;

                if (info.image && (info.image->GetUsage() & PE_IMAGE_USAGE_STORAGE))
                    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }

            return pe_dx12::ToD3D12ResourceState(info.layout);
        }

        bool CoalescePendingTransition(std::vector<D3D12_RESOURCE_BARRIER> &batch,
                                       ID3D12Resource *resource,
                                       UINT subresource,
                                       D3D12_RESOURCE_STATES after,
                                       bool *transitionPending = nullptr)
        {
            if (transitionPending)
                *transitionPending = false;

            for (auto it = batch.rbegin(); it != batch.rend(); ++it)
            {
                D3D12_RESOURCE_BARRIER &rb = *it;
                if (rb.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV &&
                    (!rb.UAV.pResource || rb.UAV.pResource == resource))
                    return false;

                if (rb.Type != D3D12_RESOURCE_BARRIER_TYPE_TRANSITION ||
                    rb.Transition.pResource != resource ||
                    rb.Transition.Subresource != subresource)
                    continue;

                rb.Transition.StateAfter = after;
                if (rb.Transition.StateBefore == rb.Transition.StateAfter)
                {
                    auto eraseIt = it.base();
                    --eraseIt;
                    batch.erase(eraseIt);
                }
                else if (transitionPending)
                {
                    *transitionPending = true;
                }
                return true;
            }
            return false;
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

            if (CoalescePendingTransition(batch, buf->GetResource(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, after))
            {
                buf->m_state = after;
                return;
            }

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
            const D3D12_RESOURCE_STATES requested = ToD3D12BufferState(info);
            const bool previousUavWrite = (previous.accessMask & (PE_ACCESS_SHADER_WRITE | PE_ACCESS_SHADER_STORAGE_WRITE)) != 0;
            if (previousUavWrite && (buf->m_state & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) &&
                requested == D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
                PushBufferUAV(batch, buf);

            PushBufferTransition(batch, buf, requested);

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

    ID3D12Resource *GetDx12BufferResource(Buffer *buffer)
    {
        return buffer ? Dx12BufferImpl::From(buffer)->GetResource() : nullptr;
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
        m_owner->ClearBoundGraphicsState();

        if (!m_owner->m_afterWaitCallbacks.IsEmpty())
        {
            m_owner->m_afterWaitCallbacks.ReverseInvoke();
            m_owner->m_afterWaitCallbacks.Clear();
        }

        m_barrierBatch.clear();
        m_pendingImageBarrierRegion.clear();
        m_heapsBound = false;
        m_externalRenderPipelineBound = false;
        m_externalComputePipelineBound = false;
        m_externalVertexBindingStrides.clear();
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
        PE_PROFILE_SCOPE("DX12 FlushBarriers");
        if (m_barrierBatch.empty())
        {
            m_pendingImageBarrierRegion.clear();
            return;
        }

        const bool markImageBarrier = !m_pendingImageBarrierRegion.empty();
        if (markImageBarrier)
            BeginDebugRegion(m_pendingImageBarrierRegion);

        PE_PROFILE_COUNTER("DX12 ResourceBarrier Items", m_barrierBatch.size());
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
    // DX12 today: PushDescriptor (zero callers tree-wide), SetEvent (only
    // reachable via the Lua CommandBindings::SetEvent shim, no script in the
    // repo invokes it). Macro fires PE_ERROR if a future caller hits them so we
    // notice instead of silently no-oping.
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
        thread_local std::vector<ImageBarrierInfo> barriers;
        barriers.resize(images.size());
        for (size_t i = 0; i < images.size(); ++i)
        {
            PE_ERROR_IF(!images[i], "Dx12CommandBufferImpl::ClearColors: image %zu is null", i);
            PE_ERROR_IF(PeFormatHasDepthOrStencil(images[i]->GetFormat()),
                        "Dx12CommandBufferImpl::ClearColors: image '%s' is depth/stencil",
                        images[i]->GetName().c_str());
            barriers[i] = {};
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
            const D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetAttachmentCpuHandle(image, nullptr, false, "ClearColors");
            m_cmdList->ClearRenderTargetView(rtv, color, 0, nullptr);
        }
    }

    void Dx12CommandBufferImpl::ClearDepthStencils(std::vector<Image *> images)
    {
        thread_local std::vector<ImageBarrierInfo> barriers;
        barriers.resize(images.size());
        for (size_t i = 0; i < images.size(); ++i)
        {
            PE_ERROR_IF(!images[i], "Dx12CommandBufferImpl::ClearDepthStencils: image %zu is null", i);
            PE_ERROR_IF(!PeFormatHasDepthOrStencil(images[i]->GetFormat()),
                        "Dx12CommandBufferImpl::ClearDepthStencils: image '%s' is not depth/stencil",
                        images[i]->GetName().c_str());
            barriers[i] = {};
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
            if (PeFormatHasStencil(image->GetFormat()))
                flags |= D3D12_CLEAR_FLAG_STENCIL;
            const D3D12_CPU_DESCRIPTOR_HANDLE dsv = GetAttachmentCpuHandle(image, nullptr, true, "ClearDepthStencils");
            m_cmdList->ClearDepthStencilView(dsv, flags, depth, stencil, 0, nullptr);
        }
    }

    void Dx12CommandBufferImpl::BeginPass(uint32_t count,
                                          Attachment *attachments,
                                          const std::string &name,
                                          bool /*skipDynamicPass*/)
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

        thread_local std::vector<ImageBarrierInfo> attachmentBarriers;
        attachmentBarriers.clear();
        attachmentBarriers.reserve(count);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        uint32_t rtvCount = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
        bool hasDsv = false;

        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const Attachment &att = attachments[i];
                PE_ERROR_IF(!att.image, "Dx12CommandBufferImpl::BeginPass: attachment %u has null image", i);

                const ::PeFormat fmt = att.image->GetFormat();
                const bool isDepthStencil = PeFormatHasDepthOrStencil(fmt);

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
                    dsvHandle = GetAttachmentCpuHandle(att.image, att.view, true, "BeginPass");
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
                    rtvHandles[rtvCount++] = GetAttachmentCpuHandle(att.image, att.view, false, "BeginPass");
                }

                attachmentBarriers.push_back(barrier);
            }
        }

        Image::Barriers(m_owner, attachmentBarriers);
        FlushBarriers();

        m_cmdList->OMSetRenderTargets(rtvCount,
                                      rtvCount > 0 ? rtvHandles : nullptr,
                                      FALSE,
                                      hasDsv ? &dsvHandle : nullptr);

        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const Attachment &att = attachments[i];
                const ::PeFormat fmt = att.image->GetFormat();

                if (PeFormatHasDepthOrStencil(fmt))
                {
                    D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(0);
                    if (att.loadOp == PE_LOAD_OP_CLEAR)
                        flags |= D3D12_CLEAR_FLAG_DEPTH;
                    if (PeFormatHasStencil(fmt) && att.stencilLoadOp == PE_LOAD_OP_CLEAR)
                        flags |= D3D12_CLEAR_FLAG_STENCIL;
                    if (flags == 0)
                        continue;

                    const float depth = att.image->m_clearColor[0];
                    const uint8_t stencil = static_cast<uint8_t>(att.image->m_clearColor[1]);
                    m_cmdList->ClearDepthStencilView(GetAttachmentCpuHandle(att.image, att.view, true, "BeginPass"),
                                                     flags, depth, stencil, 0, nullptr);
                }
                else if (att.loadOp == PE_LOAD_OP_CLEAR)
                {
                    const vec4 &c = att.image->m_clearColor;
                    const float color[4] = {c[0], c[1], c[2], c[3]};
                    m_cmdList->ClearRenderTargetView(GetAttachmentCpuHandle(att.image, att.view, false, "BeginPass"),
                                                     color, 0, nullptr);
                }
            }
        }
    }

    void Dx12CommandBufferImpl::EndPass()
    {
        m_owner->FixupLoadOpAttachmentWriteAccess();

        // DX12 has no end-render-pass call in the legacy command-list path; just
        // unbind so the next non-pass command (e.g. a copy) doesn't trip over a
        // stale RTV/DSV binding.
        m_cmdList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);

        EndDebugRegion();

        m_owner->ClearBoundGraphicsState();
    }

    void Dx12CommandBufferImpl::BindPipeline(PassInfo &passInfo, bool bindDescriptors)
    {
        Pipeline *pipeline = CommandBuffer::GetPipeline(m_owner->m_renderPass, passInfo);
        PE_ERROR_IF(!pipeline, "Dx12CommandBufferImpl::BindPipeline: pipeline creation failed");

        if (pipeline != m_owner->m_boundPipeline)
        {
            m_owner->m_boundPipeline = pipeline;
            m_externalRenderPipelineBound = false;
            m_externalComputePipelineBound = false;
            m_externalVertexBindingStrides.clear();

            Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
            PE_ERROR_IF(!rhi || !rhi->GetSharedRootSig(), "Dx12CommandBufferImpl::BindPipeline: shared root signature unavailable");

            ID3D12RootSignature *rootSig = rhi->GetSharedRootSig()->Get();

            BindShaderVisibleHeaps();

            if (IsRayTracingPipeline(pipeline))
            {
                ID3D12StateObject *stateObject = GetDx12RayTracingStateObject(pipeline);
                PE_ERROR_IF(!stateObject, "Dx12CommandBufferImpl::BindPipeline: ray tracing pipeline has no state object");
                Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
                PE_CHECK(m_cmdList.As(&cmdList4));
                cmdList4->SetPipelineState1(stateObject);
                m_cmdList->SetComputeRootSignature(rootSig);
            }
            else
            {
                ID3D12PipelineState *pso = GetDx12Pipeline(pipeline);
                PE_ERROR_IF(!pso, "Dx12CommandBufferImpl::BindPipeline: pipeline has no PSO");
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
        PE_ERROR_IF(!m_owner->m_boundPipeline && !m_externalRenderPipelineBound, "Dx12CommandBufferImpl::BindVertexBuffer: no bound pipeline (DX12 needs stride from PSO reflection)");
        PE_ERROR_IF(offset > buffer->Size(), "Dx12CommandBufferImpl::BindVertexBuffer: offset exceeds buffer size");

        PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(buffer), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

        m_owner->m_boundVertexBuffer = buffer;
        m_owner->m_boundVertexBufferOffset = offset;
        m_owner->m_boundVertexBufferFirstBinding = firstBinding;
        m_owner->m_boundVertexBufferBindingCount = bindingCount;

        const Dx12BufferImpl *bufImpl = Dx12BufferImpl::From(buffer);

        thread_local std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
        views.assign(bindingCount, D3D12_VERTEX_BUFFER_VIEW{});
        for (uint32_t i = 0; i < bindingCount; ++i)
        {
            const uint32_t binding = firstBinding + i;
            D3D12_VERTEX_BUFFER_VIEW &view = views[i];
            view.BufferLocation = bufImpl->GetResource()->GetGPUVirtualAddress() + offset;
            view.SizeInBytes = static_cast<UINT>(buffer->Size() - offset);
            if (m_externalRenderPipelineBound)
            {
                view.StrideInBytes = binding < m_externalVertexBindingStrides.size()
                                         ? m_externalVertexBindingStrides[binding]
                                         : 0;
            }
            else
            {
                const Dx12PipelineImpl *pipelineImpl = Dx12PipelineImpl::From(m_owner->m_boundPipeline);
                view.StrideInBytes = pipelineImpl->GetVertexBindingStride(binding);
            }
            PE_ERROR_IF(view.StrideInBytes == 0,
                        "Dx12CommandBufferImpl::BindVertexBuffer: stride for binding %u is zero (pipeline reflection produced no input)",
                        binding);
        }

        m_cmdList->IASetVertexBuffers(firstBinding, bindingCount, views.data());
    }

    void Dx12CommandBufferImpl::BindIndexBuffer(Buffer *buffer, size_t offset, PeIndexType indexType)
    {
        if (m_owner->m_boundIndexBuffer == buffer && m_owner->m_boundIndexBufferOffset == offset &&
            m_owner->m_boundIndexBufferType == indexType)
            return;

        PE_ERROR_IF(!buffer, "Dx12CommandBufferImpl::BindIndexBuffer: null buffer");
        PE_ERROR_IF(offset > buffer->Size(), "Dx12CommandBufferImpl::BindIndexBuffer: offset exceeds buffer size");

        PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(buffer), D3D12_RESOURCE_STATE_INDEX_BUFFER);

        m_owner->m_boundIndexBuffer = buffer;
        m_owner->m_boundIndexBufferOffset = offset;
        m_owner->m_boundIndexBufferType = indexType;

        const Dx12BufferImpl *bufImpl = Dx12BufferImpl::From(buffer);

        D3D12_INDEX_BUFFER_VIEW view{};
        view.BufferLocation = bufImpl->GetResource()->GetGPUVirtualAddress() + offset;
        view.SizeInBytes = static_cast<UINT>(buffer->Size() - offset);
        view.Format = IndexFormat(indexType);

        m_cmdList->IASetIndexBuffer(&view);
    }

    void Dx12CommandBufferImpl::BindDescriptors(uint32_t count, Descriptor *const *descriptors)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "Dx12CommandBufferImpl::BindDescriptors: No bound pipeline found!");
        PE_ERROR_IF(count > 0 && !descriptors, "Dx12CommandBufferImpl::BindDescriptors: null descriptor array");

        const bool compute = UsesComputeRootSignature(m_owner->m_boundPipeline);
        BindDescriptorTables(count, descriptors, compute);
    }

    void Dx12CommandBufferImpl::BindExternalRenderPipeline(ID3D12PipelineState *pipeline,
                                                           D3D12_PRIMITIVE_TOPOLOGY topology,
                                                           const std::vector<uint32_t> &vertexBindingStrides)
    {
        PE_ERROR_IF(!pipeline, "Dx12CommandBufferImpl::BindExternalRenderPipeline: null pipeline");

        Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetSharedRootSig(), "Dx12CommandBufferImpl::BindExternalRenderPipeline: shared root signature unavailable");

        m_owner->m_boundPipeline = nullptr;
        m_owner->m_boundVertexBuffer = nullptr;
        m_owner->m_boundVertexBufferOffset = -1;
        m_owner->m_boundVertexBufferFirstBinding = UINT32_MAX;
        m_owner->m_boundVertexBufferBindingCount = UINT32_MAX;
        m_externalRenderPipelineBound = true;
        m_externalComputePipelineBound = false;
        m_externalVertexBindingStrides = vertexBindingStrides;

        BindShaderVisibleHeaps();
        m_cmdList->SetPipelineState(pipeline);
        m_cmdList->SetGraphicsRootSignature(rhi->GetSharedRootSig()->Get());
        if (topology != m_lastTopology)
        {
            m_cmdList->IASetPrimitiveTopology(topology);
            m_lastTopology = topology;
        }
    }

    void Dx12CommandBufferImpl::BindExternalRenderDescriptors(uint32_t count, Descriptor *const *descriptors)
    {
        PE_ERROR_IF(!m_externalRenderPipelineBound, "Dx12CommandBufferImpl::BindExternalRenderDescriptors: no external render pipeline bound");
        PE_ERROR_IF(count > 0 && !descriptors, "Dx12CommandBufferImpl::BindExternalRenderDescriptors: null descriptor array");
        BindDescriptorTables(count, descriptors, false);
    }

    void Dx12CommandBufferImpl::BindExternalRenderDescriptorSpace(Descriptor *descriptor, uint32_t sourceDxSpace, uint32_t targetDxSpace)
    {
        PE_ERROR_IF(!m_externalRenderPipelineBound, "Dx12CommandBufferImpl::BindExternalRenderDescriptorSpace: no external render pipeline bound");
        BindDescriptorTableSpace(descriptor, sourceDxSpace, targetDxSpace, false);
    }

    void Dx12CommandBufferImpl::BindExternalComputePipeline(ID3D12PipelineState *pipeline)
    {
        PE_ERROR_IF(!pipeline, "Dx12CommandBufferImpl::BindExternalComputePipeline: null pipeline");

        Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->GetSharedRootSig(), "Dx12CommandBufferImpl::BindExternalComputePipeline: shared root signature unavailable");

        m_owner->m_boundPipeline = nullptr;
        m_externalRenderPipelineBound = false;
        m_externalComputePipelineBound = true;
        m_externalVertexBindingStrides.clear();

        BindShaderVisibleHeaps();
        m_cmdList->SetPipelineState(pipeline);
        m_cmdList->SetComputeRootSignature(rhi->GetSharedRootSig()->Get());
    }

    void Dx12CommandBufferImpl::BindExternalComputeDescriptors(uint32_t count, Descriptor *const *descriptors)
    {
        PE_ERROR_IF(!m_externalComputePipelineBound, "Dx12CommandBufferImpl::BindExternalComputeDescriptors: no external compute pipeline bound");
        PE_ERROR_IF(count > 0 && !descriptors, "Dx12CommandBufferImpl::BindExternalComputeDescriptors: null descriptor array");
        BindDescriptorTables(count, descriptors, true);
    }

    void Dx12CommandBufferImpl::BindExternalComputeDescriptorSpace(Descriptor *descriptor, uint32_t sourceDxSpace, uint32_t targetDxSpace)
    {
        PE_ERROR_IF(!m_externalComputePipelineBound, "Dx12CommandBufferImpl::BindExternalComputeDescriptorSpace: no external compute pipeline bound");
        BindDescriptorTableSpace(descriptor, sourceDxSpace, targetDxSpace, true);
    }

    void Dx12CommandBufferImpl::BindDescriptorTableSpace(Descriptor *descriptor, uint32_t sourceDxSpace, uint32_t targetDxSpace, bool compute)
    {
        PE_ERROR_IF(!descriptor, "Dx12CommandBufferImpl::BindDescriptorTableSpace: null descriptor");
        PE_ERROR_IF(sourceDxSpace >= DX12_DESCRIPTOR_SPACE_COUNT,
                    "Dx12CommandBufferImpl::BindDescriptorTableSpace: source space %u exceeds supported count %u",
                    sourceDxSpace,
                    DX12_DESCRIPTOR_SPACE_COUNT);
        PE_ERROR_IF(targetDxSpace >= DX12_DESCRIPTOR_SPACE_COUNT,
                    "Dx12CommandBufferImpl::BindDescriptorTableSpace: target space %u exceeds supported count %u",
                    targetDxSpace,
                    DX12_DESCRIPTOR_SPACE_COUNT);

        BindShaderVisibleHeaps();

        const Dx12DescriptorImpl *impl = Dx12DescriptorImpl::From(descriptor);

        const D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvUavTable = impl->GetCbvSrvUavTableGpuHandle(sourceDxSpace);
        if (cbvSrvUavTable.ptr != 0)
        {
            const uint32_t rootIdx = Dx12CbvSrvUavRootIndex(targetDxSpace);
            if (compute)
                m_cmdList->SetComputeRootDescriptorTable(rootIdx, cbvSrvUavTable);
            else
                m_cmdList->SetGraphicsRootDescriptorTable(rootIdx, cbvSrvUavTable);
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE samplerTable = impl->GetSamplerTableGpuHandle(sourceDxSpace);
        if (samplerTable.ptr != 0)
        {
            const uint32_t rootIdx = Dx12SamplerRootIndex(targetDxSpace);
            if (compute)
                m_cmdList->SetComputeRootDescriptorTable(rootIdx, samplerTable);
            else
                m_cmdList->SetGraphicsRootDescriptorTable(rootIdx, samplerTable);
        }
    }

    void Dx12CommandBufferImpl::BindDescriptorTables(uint32_t count, Descriptor *const *descriptors, bool compute)
    {
        BindShaderVisibleHeaps();

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

    void Dx12CommandBufferImpl::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
        D3D12_VIEWPORT vp{};
        vp.TopLeftX = x;
        vp.TopLeftY = y;
        vp.Width = width;
        vp.Height = height;
        vp.MinDepth = minDepth;
        vp.MaxDepth = maxDepth;
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
    void Dx12CommandBufferImpl::SetBlendConstants(const float constants[4])
    {
        m_cmdList->OMSetBlendFactor(constants);
    }
    void Dx12CommandBufferImpl::SetStencilReference(uint32_t reference)
    {
        m_cmdList->OMSetStencilRef(reference);
    }
    void Dx12CommandBufferImpl::SetLineWidth(float)
    {
        // DX12 has no concept of dynamic line width; lines are 1px and the
        // raster state is baked into the PSO. The Vulkan path keeps it dynamic,
        // so this surface stays callable but is a no-op on DX12.
    }
    void Dx12CommandBufferImpl::SetDepthBias(float, float, float)
    {
        // DX12 bakes depth bias into the PSO rasterizer state. Runtime tuning
        // requires a new PSO; current callers rely on PassInfo's baked values.
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
    void Dx12CommandBufferImpl::DispatchExternalCompute(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        PE_ERROR_IF(!m_externalComputePipelineBound, "Dx12CommandBufferImpl::DispatchExternalCompute: no external compute pipeline bound");
        FlushBarriers();
        m_cmdList->Dispatch(groupCountX, groupCountY, groupCountZ);
    }
    void Dx12CommandBufferImpl::PushConstants(const PushConstantsBlock<128> &constants)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "Dx12CommandBufferImpl::PushConstants: No bound pipeline found!");

        const auto &sizes = m_owner->m_boundPipeline->GetPushConstantSizes();
        if (sizes.empty() || sizes[0] == 0)
            return;

        // Dx12PipelineImpl collapses vert+frag push ranges into a single 0-based
        // entry sized to the larger of the two. Root constants live at b0/space0
        // (DX12_ROOT_CONSTANTS_INDEX) per the shared root signature layout.
        const uint32_t numDwords = (sizes[0] + 3u) / 4u;
        PE_ERROR_IF(numDwords > 32, "Dx12CommandBufferImpl::PushConstants: push constants exceed shared root constant budget");
        const bool compute = UsesComputeRootSignature(m_owner->m_boundPipeline);

        if (compute)
            m_cmdList->SetComputeRoot32BitConstants(DX12_ROOT_CONSTANTS_INDEX, numDwords, constants.Data(), 0);
        else
            m_cmdList->SetGraphicsRoot32BitConstants(DX12_ROOT_CONSTANTS_INDEX, numDwords, constants.Data(), 0);
    }

    void Dx12CommandBufferImpl::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        if (m_owner->m_boundPipeline)
            PE_ERROR_IF(!IsGraphicsPipeline(m_owner->m_boundPipeline), "Dx12CommandBufferImpl::Draw: bound pipeline is not graphics");
        else
            PE_ERROR_IF(!m_externalRenderPipelineBound, "Dx12CommandBufferImpl::Draw: no external render pipeline bound");
        FlushBarriers();
        m_cmdList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
    }
    void Dx12CommandBufferImpl::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        if (m_owner->m_boundPipeline)
            PE_ERROR_IF(!IsGraphicsPipeline(m_owner->m_boundPipeline), "Dx12CommandBufferImpl::DrawIndexed: bound pipeline is not graphics");
        else
            PE_ERROR_IF(!m_externalRenderPipelineBound, "Dx12CommandBufferImpl::DrawIndexed: no external render pipeline bound");
        FlushBarriers();
        m_cmdList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }
    void Dx12CommandBufferImpl::DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        static_assert(PE_DRAW_INDIRECT_COMMAND_SIZE == sizeof(D3D12_DRAW_ARGUMENTS));
        ValidateIndirectDrawState(m_owner->m_boundPipeline, m_externalRenderPipelineBound, "DrawIndirect");
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
        ValidateIndirectDrawState(m_owner->m_boundPipeline, m_externalRenderPipelineBound, "DrawIndexedIndirect");
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
        ValidateIndirectDrawState(m_owner->m_boundPipeline, m_externalRenderPipelineBound, "DrawIndexedIndirectCount");
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

        // GPU-native fill: copy `size` bytes from a persistent buffer pre-filled with `data` — no CPU
        // staging round-trip (alloc + memcpy + deferred free) per call. D3D12 has no vkCmdFillBuffer
        // equivalent, and the arena/cull fire many small fills per frame, so a copy from a persistent
        // source is cheaper than the legacy staging path below. Falls back to staging for unsupported
        // values or fills larger than the source. Byte-for-byte identical to the staging fill (same uint32 pattern).
        Buffer *fillSrc = RHII.GetDx12FillSource(data);
        if (fillSrc && size <= fillSrc->Size())
        {
            PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(fillSrc), D3D12_RESOURCE_STATE_COPY_SOURCE);
            PushBufferTransition(m_barrierBatch, Dx12BufferImpl::From(buffer), D3D12_RESOURCE_STATE_COPY_DEST);
            FlushBarriers();
            m_cmdList->CopyBufferRegion(Dx12BufferImpl::From(buffer)->GetResource(), static_cast<UINT64>(offset),
                                        Dx12BufferImpl::From(fillSrc)->GetResource(), 0, static_cast<UINT64>(size));
            BufferTrackInfo &trackInfo = buffer->GetTrackInfo();
            trackInfo.stageMask = PE_STAGE_CLEAR;
            trackInfo.accessMask = PE_ACCESS_TRANSFER_WRITE;
            return;
        }

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

    void Dx12CommandBufferImpl::CopyImageToBuffer(Image *src, Buffer *dst, uint32_t mipLevel, uint32_t baseArrayLayer, uint32_t layerCount)
    {
        PE_ERROR_IF(!src || !dst, "Dx12CommandBufferImpl::CopyImageToBuffer: null resource");
        src->CopyToBuffer(m_owner, dst, mipLevel, baseArrayLayer, layerCount);
    }
    void Dx12CommandBufferImpl::GenerateMipMaps(Image *image)
    {
        PE_ERROR_IF(!image, "Dx12CommandBufferImpl::GenerateMipMaps: null image");
        image->GenerateMipMaps(m_owner);
    }

    void Dx12CommandBufferImpl::ResetQueryPool(QueryPool *pool, uint32_t firstQuery, uint32_t queryCount)
    {
        // D3D12 query heaps have no explicit reset; a slot is reused on the next Begin/EndQuery.
        (void)pool;
        (void)firstQuery;
        (void)queryCount;
    }

    void Dx12CommandBufferImpl::BeginQuery(QueryPool *pool, uint32_t queryIndex, PeQueryControlFlags flags)
    {
        PE_ERROR_IF(pool->GetType() != PE_QUERY_TYPE_OCCLUSION, "BeginQuery: pool is not an occlusion query pool");
        (void)flags; // DX12 occlusion queries are always precise; no per-begin control flags
        Dx12QueryPool *qp = Dx12QueryPool::From(pool);
        m_cmdList->BeginQuery(qp->Heap(), qp->QueryType(), queryIndex);
    }

    void Dx12CommandBufferImpl::EndQuery(QueryPool *pool, uint32_t queryIndex)
    {
        PE_ERROR_IF(pool->GetType() != PE_QUERY_TYPE_OCCLUSION, "EndQuery: pool is not an occlusion query pool");
        Dx12QueryPool *qp = Dx12QueryPool::From(pool);
        m_cmdList->EndQuery(qp->Heap(), qp->QueryType(), queryIndex);
    }

    void Dx12CommandBufferImpl::WriteTimestamp(QueryPool *pool, uint32_t queryIndex)
    {
        // A timestamp is recorded via EndQuery on a TIMESTAMP heap (no Begin).
        PE_ERROR_IF(pool->GetType() != PE_QUERY_TYPE_TIMESTAMP, "WriteTimestamp: pool is not a timestamp query pool");
        Dx12QueryPool *qp = Dx12QueryPool::From(pool);
        m_cmdList->EndQuery(qp->Heap(), D3D12_QUERY_TYPE_TIMESTAMP, queryIndex);
    }

    void Dx12CommandBufferImpl::ResolveQueryPool(QueryPool *pool, uint32_t firstQuery, uint32_t queryCount,
                                                 Buffer *dst, uint64_t dstOffset, uint64_t stride, PeQueryResultFlags flags)
    {
        // DX12 ResolveQueryData always writes fixed 64-bit (8-byte) results with no availability
        // word; a non-64-bit/non-default stride or WITH_AVAILABILITY would silently diverge from
        // the requested layout, so reject them rather than ignore (WAIT is implicit, safe to drop).
        PE_ERROR_IF(!(flags & PE_QUERY_RESULT_64_BIT) || stride != sizeof(uint64_t) ||
                        (flags & PE_QUERY_RESULT_WITH_AVAILABILITY),
                    "Dx12CommandBufferImpl::ResolveQueryPool: DX12 supports only 64-bit 8-byte results without availability");
        FlushBarriers(); // apply any batched dst (COPY_DEST) transition before resolving
        Dx12QueryPool *qp = Dx12QueryPool::From(pool);
        ID3D12Resource *dstResource = GetDx12BufferResource(dst);
        m_cmdList->ResolveQueryData(qp->Heap(), qp->QueryType(), firstQuery, queryCount, dstResource, dstOffset);
    }

    void Dx12CommandBufferImpl::TraceRays(uint32_t width, uint32_t height, uint32_t depth)
    {
        PE_ERROR_IF(!m_owner->m_boundPipeline, "Dx12CommandBufferImpl::TraceRays: No bound pipeline found!");
        PE_ERROR_IF(!IsRayTracingPipeline(m_owner->m_boundPipeline), "Dx12CommandBufferImpl::TraceRays: bound pipeline is not ray tracing");

        Dx12RhiImpl *rhi = static_cast<Dx12RhiImpl *>(RHII.GetImpl());
        PE_ERROR_IF(!rhi || !rhi->SupportsDxr(), "Dx12CommandBufferImpl::TraceRays: DXR is not supported on this device");

        const Dx12PipelineImpl *pipeline = Dx12PipelineImpl::From(m_owner->m_boundPipeline);
        ID3D12StateObject *stateObject = pipeline->GetRtStateObject();
        PE_ERROR_IF(!stateObject, "Dx12CommandBufferImpl::TraceRays: pipeline has no DXR state object");

        D3D12_DISPATCH_RAYS_DESC desc = pipeline->GetDispatchRaysDesc(width, height, depth);
        PE_ERROR_IF(desc.RayGenerationShaderRecord.StartAddress == 0 || desc.RayGenerationShaderRecord.SizeInBytes == 0,
                    "Dx12CommandBufferImpl::TraceRays: ray generation shader table is empty");

        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cmdList4;
        PE_CHECK(m_cmdList.As(&cmdList4));

        BindShaderVisibleHeaps();
        FlushBarriers();
        cmdList4->SetPipelineState1(stateObject);
        cmdList4->DispatchRays(&desc);
    }

    namespace
    {
        bool HasWriteAccess(PeBarrierAccess accessMask)
        {
            constexpr PeBarrierAccess writeMask =
                PE_ACCESS_SHADER_WRITE |
                PE_ACCESS_SHADER_STORAGE_WRITE |
                PE_ACCESS_COLOR_ATTACHMENT_WRITE |
                PE_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE |
                PE_ACCESS_TRANSFER_WRITE |
                PE_ACCESS_HOST_WRITE |
                PE_ACCESS_MEMORY_WRITE |
                PE_ACCESS_ACCELERATION_STRUCTURE_WRITE_KHR;
            return (accessMask & writeMask) != 0;
        }

        bool PushImageStateTransition(std::vector<D3D12_RESOURCE_BARRIER> &batch,
                                      Dx12ImageImpl *img,
                                      D3D12_RESOURCE_STATES after)
        {
            const D3D12_RESOURCE_STATES before = img->m_state;
            if (before == after)
                return false;

            bool transitionPending = false;
            if (CoalescePendingTransition(batch, img->GetResource(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, after, &transitionPending))
            {
                img->m_state = after;
                return transitionPending;
            }

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

    void Dx12CommandBufferImpl::PrepareImageForFirstWrite(Dx12ImageImpl *img,
                                                          D3D12_RESOURCE_STATES requestedState,
                                                          PeBarrierAccess accessMask)
    {
        if (!img || !img->GetResource() || !img->m_needsFirstUseDiscard || !HasWriteAccess(accessMask))
            return;

        const D3D12_RESOURCE_STATES discardState = img->m_firstUseDiscardState;
        if (discardState == D3D12_RESOURCE_STATE_COMMON)
            return;

        if (PushImageStateTransition(m_barrierBatch, img, discardState))
            FlushBarriers();
        else if (!m_barrierBatch.empty())
            FlushBarriers();

        m_cmdList->DiscardResource(img->GetResource(), nullptr);
        img->m_needsFirstUseDiscard = false;

        if (requestedState != discardState)
            PushImageStateTransition(m_barrierBatch, img, requestedState);
    }

    void Dx12CommandBufferImpl::BufferBarrier(const BufferBarrierInfo &info)
    {
        PE_PROFILE_COUNTER("DX12 Queue BufferBarrier Items", 1);
        PushBufferBarrier(m_barrierBatch, info);
    }
    void Dx12CommandBufferImpl::BufferBarriers(const std::vector<BufferBarrierInfo> &infos)
    {
        PE_PROFILE_COUNTER("DX12 Queue BufferBarrier Items", infos.size());
        m_barrierBatch.reserve(m_barrierBatch.size() + infos.size());
        for (const auto &info : infos)
            PushBufferBarrier(m_barrierBatch, info);
    }
    void Dx12CommandBufferImpl::ImageBarrier(const ImageBarrierInfo &info)
    {
        PE_PROFILE_COUNTER("DX12 Queue ImageBarrier Items", 1);
        if (!info.image)
            return;
        Dx12ImageImpl *img = Dx12ImageImpl::From(info.image);
        const D3D12_RESOURCE_STATES after = ToD3D12ImageState(info);
        PrepareImageForFirstWrite(img, after, info.accessMask);
        if (PushImageStateTransition(m_barrierBatch, img, after))
            MarkPendingImageBarrierRegion("ImageBarrier");
    }
    void Dx12CommandBufferImpl::ImageBarriers(const std::vector<ImageBarrierInfo> &infos)
    {
        PE_PROFILE_COUNTER("DX12 Queue ImageBarrier Items", infos.size());
        m_barrierBatch.reserve(m_barrierBatch.size() + infos.size());
        for (const auto &info : infos)
        {
            if (!info.image)
                continue;
            Dx12ImageImpl *img = Dx12ImageImpl::From(info.image);
            const D3D12_RESOURCE_STATES after = ToD3D12ImageState(info);
            PrepareImageForFirstWrite(img, after, info.accessMask);
            if (PushImageStateTransition(m_barrierBatch, img, after))
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
