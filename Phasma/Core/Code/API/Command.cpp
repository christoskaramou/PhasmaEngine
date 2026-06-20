#include "API/Command.h"
#include "API/Buffer.h"
#include "API/CommandBuffer_Internal.h"
#include "API/Event.h"
#include "API/Framebuffer.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Semaphore.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"
#if defined(PE_WIN32)
#include "API/DX12/Dx12CommandBufferImpl.h"
#endif

namespace pe
{
    namespace
    {
        ImageBarrierInfo NormalizeImageBarrierForBackend(const ImageBarrierInfo &info)
        {
            ImageBarrierInfo normalized = info;
            if (normalized.layout == PE_IMAGE_LAYOUT_PRESENT_SRC &&
                normalized.stageFlags == PE_STAGE_NONE &&
                (RHII.GetApi() == PE_GRAPHICS_API_DX12 || RHII.UsesDozenVulkan()))
            {
                // DX12 and Dozen reject PE_STAGE_NONE on present transitions.
                normalized.stageFlags = PE_STAGE_ALL_COMMANDS;
            }
            return normalized;
        }
    } // namespace

    CommandBuffer::CommandBuffer(CommandPool *commandPool, const std::string &name)
        : m_id{ID::NextID()},
          m_submission{0},
          m_renderPass{nullptr},
          m_boundPipeline{nullptr},
          m_commandPool{commandPool},
          m_event{RHII.GetApi() == PE_GRAPHICS_API_VULKAN ? Event::Create(name + "_event") : nullptr},
          m_name{name},
          m_threadId{std::this_thread::get_id()}
    {
        m_impl = CreateCommandBufferImpl(this, commandPool, name);
        PE_ERROR_IF(!m_impl, "CommandBuffer: backend returned null Impl from CreateCommandBufferImpl");
    }

    CommandBuffer::~CommandBuffer()
    {
        // Pending after-wait callbacks here mean a recording was never submitted+waited
        // (encoder dropped, pool teardown, validation rejection). The GPU never observed
        // the work, so it's safe — and necessary — to run them now to release any
        // captured staging allocations or refcounts.
        if (!m_afterWaitCallbacks.IsEmpty())
        {
            m_afterWaitCallbacks.ReverseInvoke();
            m_afterWaitCallbacks.Clear();
        }

        Event::Destroy(m_event);

#if PE_DEBUG_MODE
        for (auto &gpuTimerInfo : m_gpuTimerInfos)
            GpuTimer::Destroy(gpuTimerInfo.timer);
#endif

        delete m_impl;
        m_impl = nullptr;
    }

    void CommandBuffer::Begin()
    {
        PE_PROFILE_COUNTER("Cmd Begin Calls", 1);
        PE_PROFILE_SCOPE("Cmd Begin");
        {
            PE_PROFILE_SCOPE("Cmd Backend Begin");
            m_impl->Begin();
        }
        {
            PE_PROFILE_SCOPE("Cmd Begin Debug Region");
            // Neutral outer debug-region wrap: drives the per-frame GpuTimer that
            // ProfilerWidget reads, and gives every per-pass timer a depth >= 1
            // (the GPU/Table widget hides depth==0 entries).
            BeginDebugRegion(m_name);
        }
    }

    void CommandBuffer::End()
    {
        PE_PROFILE_COUNTER("Cmd End Calls", 1);
        PE_PROFILE_SCOPE("Cmd End");
        {
            PE_PROFILE_SCOPE("Cmd End Debug Region");
            EndDebugRegion();
        }
        {
            PE_PROFILE_SCOPE("Cmd Backend End");
            m_impl->End();
        }
    }

    void CommandBuffer::Reset()
    {
        m_impl->Reset();
    }

    void CommandBuffer::SetDepthBias(float constantFactor, float clamp, float slopeFactor)
    {
        m_impl->SetDepthBias(constantFactor, clamp, slopeFactor);
    }

    void CommandBuffer::BlitImage(Image *src, Image *dst, const ImageBlit &region, PeFilter filter)
    {
        m_impl->BlitImage(src, dst, region, filter);
    }

    void CommandBuffer::ClearColors(std::vector<Image *> images)
    {
        m_impl->ClearColors(std::move(images));
    }

    void CommandBuffer::ClearDepthStencils(std::vector<Image *> images)
    {
        m_impl->ClearDepthStencils(std::move(images));
    }

    // Note: We don't care about the actual images, having the Attachment struct for convenience
    RenderPass *CommandBuffer::GetRenderPass(uint32_t count, Attachment *attachments)
    {
        PE_PROFILE_SCOPE("Cmd GetRenderPass");
        Hash hash;
        hash.Combine(count);
        for (uint32_t i = 0; i < count; i++)
        {
            const Attachment &attachment = attachments[i];
            hash.Combine(static_cast<uint32_t>(attachment.image->GetFormat()));
            hash.Combine(static_cast<uint32_t>(attachment.image->GetSamples()));
            hash.Combine(static_cast<uint32_t>(attachment.loadOp));
            hash.Combine(static_cast<uint32_t>(attachment.storeOp));
            hash.Combine(static_cast<uint32_t>(attachment.stencilLoadOp));
            hash.Combine(static_cast<uint32_t>(attachment.stencilStoreOp));
        }

        auto it = s_renderPasses.find(hash);
        if (it != s_renderPasses.end())
        {
            return it->second;
        }
        else
        {
            PE_PROFILE_SCOPE("Cmd CreateRenderPass");
            std::string name = "Auto_Gen_RenderPass_" + std::to_string(s_renderPasses.size());
            RenderPass *newRenderPass = RenderPass::Create(count, attachments, name);
            s_renderPasses[hash] = newRenderPass;

            return newRenderPass;
        }
    }

    // Note: Cares only about the actual images, just having the Attachment struct for convenience
    Framebuffer *CommandBuffer::GetFramebuffer(RenderPass *renderPass, uint32_t count, Attachment *attachments)
    {
        PE_PROFILE_SCOPE("Cmd GetFramebuffer");
        std::vector<ImageView *> views{};
        views.reserve(count);

        Hash hash;
        hash.Combine(reinterpret_cast<std::intptr_t>(renderPass));
        for (uint32_t i = 0; i < count; i++)
        {
            Image *image = attachments[i].image;
            ImageView *view = attachments[i].view;
            if (!view)
            {
                if (!image->HasRTV())
                    image->CreateRTV();
                view = image->GetRTV();
            }

            views.push_back(view);
            hash.Combine(reinterpret_cast<std::intptr_t>(view));
        }

        auto it = s_framebuffers.find(hash);
        if (it != s_framebuffers.end())
        {
            return it->second;
        }
        else
        {
            PE_PROFILE_SCOPE("Cmd CreateFramebuffer");
            std::string name = "Auto_Gen_Framebuffer_" + std::to_string(s_framebuffers.size());
            Framebuffer *newFramebuffer = Framebuffer::Create(attachments[0].image->GetWidth(),
                                                              attachments[0].image->GetHeight(),
                                                              views,
                                                              renderPass,
                                                              name);
            s_framebuffers[hash] = newFramebuffer;

            return newFramebuffer;
        }
    }

    void CommandBuffer::BeginPass(uint32_t count,
                                  Attachment *attachments,
                                  const std::string &name,
                                  bool skipDynamicPass)
    {
        PE_PROFILE_COUNTER("Cmd BeginPass Calls", 1);
        PE_PROFILE_COUNTER("Cmd BeginPass Attachments", count);
        PE_PROFILE_SCOPE("Cmd BeginPass");
        m_impl->BeginPass(count, attachments, name, skipDynamicPass);
    }

    void CommandBuffer::EndPass()
    {
        PE_PROFILE_COUNTER("Cmd EndPass Calls", 1);
        PE_PROFILE_SCOPE("Cmd EndPass");
        m_impl->EndPass();
    }

    Pipeline *CommandBuffer::GetPipeline(RenderPass *renderPass, PassInfo &passInfo)
    {
        PE_PROFILE_SCOPE("Cmd GetPipeline");
        Hash hash;

        if (renderPass)
            hash.Combine(reinterpret_cast<std::intptr_t>(renderPass));

        hash.Combine(passInfo.GetHash());

        auto it = s_pipelines.find(hash);
        if (it != s_pipelines.end())
        {
            return it->second;
        }
        else
        {
            PE_PROFILE_SCOPE("Cmd CreatePipeline");
            Pipeline *newPipeline = Pipeline::Create(renderPass, passInfo);
            s_pipelines[hash] = newPipeline;

            return newPipeline;
        }
    }

    void CommandBuffer::ClearFramebufferCache()
    {
        for (auto &[hash, framebuffer] : s_framebuffers)
        {
            Framebuffer::Destroy(framebuffer);
        }
        s_framebuffers.clear();
    }

    void CommandBuffer::ClearCache()
    {
        for (auto &[hash, renderPass] : s_renderPasses)
        {
            RenderPass::Destroy(renderPass);
        }
        s_renderPasses.clear();

        ClearFramebufferCache();

        for (auto &[hash, pipeline] : s_pipelines)
        {
            Pipeline::Destroy(pipeline);
        }
        s_pipelines.clear();
    }

    void CommandBuffer::BindPipeline(PassInfo &passInfo, bool bindDescriptors)
    {
        PE_PROFILE_COUNTER("Cmd BindPipeline Calls", 1);
        PE_PROFILE_SCOPE("Cmd BindPipeline");
        m_impl->BindPipeline(passInfo, bindDescriptors);
    }

    void CommandBuffer::BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding, uint32_t bindingCount)
    {
        PE_PROFILE_SCOPE("Cmd BindVertexBuffer");
        m_impl->BindVertexBuffer(buffer, offset, firstBinding, bindingCount);
    }

    void CommandBuffer::BindIndexBuffer(Buffer *buffer, size_t offset, PeIndexType indexType)
    {
        PE_PROFILE_SCOPE("Cmd BindIndexBuffer");
        m_impl->BindIndexBuffer(buffer, offset, indexType);
    }

    void CommandBuffer::BindDescriptors(uint32_t count, Descriptor *const *descriptors)
    {
        PE_PROFILE_COUNTER("Cmd BindDescriptors Calls", 1);
        PE_PROFILE_COUNTER("Cmd BindDescriptors Sets", count);
        PE_PROFILE_SCOPE("Cmd BindDescriptors");
        m_impl->BindDescriptors(count, descriptors);
    }

    void CommandBuffer::PushDescriptor(uint32_t set, const std::vector<PushDescriptorInfo> &info)
    {
        PE_PROFILE_SCOPE("Cmd PushDescriptor");
        m_impl->PushDescriptor(set, info);
    }

    void CommandBuffer::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
        m_impl->SetViewport(x, y, width, height, minDepth, maxDepth);
    }

    void CommandBuffer::SetScissor(int x, int y, uint32_t width, uint32_t height)
    {
        m_impl->SetScissor(x, y, width, height);
    }

    void CommandBuffer::SetBlendConstants(const float constants[4])
    {
        m_impl->SetBlendConstants(constants);
    }

    void CommandBuffer::SetStencilReference(uint32_t reference)
    {
        m_impl->SetStencilReference(reference);
    }

    void CommandBuffer::SetLineWidth(float width)
    {
        m_impl->SetLineWidth(width);
    }

    void CommandBuffer::SetDepthTestEnable(uint32_t enable)
    {
        m_impl->SetDepthTestEnable(enable);
    }

    void CommandBuffer::SetDepthWriteEnable(uint32_t enable)
    {
        m_impl->SetDepthWriteEnable(enable);
    }

    void CommandBuffer::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        PE_PROFILE_COUNTER("Cmd Dispatch Calls", 1);
        PE_PROFILE_SCOPE("Cmd Dispatch");
        m_impl->Dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void CommandBuffer::PushConstants()
    {
        PE_PROFILE_COUNTER("Cmd PushConstants Calls", 1);
        PE_PROFILE_SCOPE("Cmd PushConstants");
        m_impl->PushConstants(m_pushConstants);
    }

    void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        PE_PROFILE_COUNTER("Cmd Draw Calls", 1);
        PE_PROFILE_SCOPE("Cmd Draw");
        m_impl->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        PE_PROFILE_COUNTER("Cmd DrawIndexed Calls", 1);
        PE_PROFILE_SCOPE("Cmd DrawIndexed");
        m_impl->DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void CommandBuffer::DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        PE_PROFILE_COUNTER("Cmd DrawIndirect Calls", 1);
        PE_PROFILE_COUNTER("Cmd DrawIndirect DrawCount", drawCount);
        PE_PROFILE_SCOPE("Cmd DrawIndirect");
        m_impl->DrawIndirect(indirectBuffer, offset, drawCount, stride);
    }

    void CommandBuffer::DrawIndexedIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        PE_PROFILE_COUNTER("Cmd DrawIndexedIndirect Calls", 1);
        PE_PROFILE_COUNTER("Cmd DrawIndexedIndirect DrawCount", drawCount);
        PE_PROFILE_SCOPE("Cmd DrawIndexedIndirect");
        m_impl->DrawIndexedIndirect(indirectBuffer, offset, drawCount, stride);
    }

    void CommandBuffer::DrawIndexedIndirectCount(Buffer *indirectBuffer, size_t offset, Buffer *countBuffer, size_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
    {
        PE_PROFILE_COUNTER("Cmd DrawIndexedIndirectCount Calls", 1);
        PE_PROFILE_COUNTER("Cmd DrawIndexedIndirectCount MaxDrawCount", maxDrawCount);
        PE_PROFILE_SCOPE("Cmd DrawIndexedIndirectCount");
        m_impl->DrawIndexedIndirectCount(indirectBuffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
    }

    void CommandBuffer::FillBuffer(Buffer *buffer, size_t offset, size_t size, uint32_t data)
    {
        PE_PROFILE_COUNTER("Cmd FillBuffer Calls", 1);
        PE_PROFILE_COUNTER("Cmd FillBuffer Bytes", size);
        PE_PROFILE_SCOPE("Cmd FillBuffer");
        m_impl->FillBuffer(buffer, offset, size, data);
    }

    void CommandBuffer::ResetQueryPool(QueryPool *pool, uint32_t firstQuery, uint32_t queryCount)
    {
        PE_PROFILE_SCOPE("Cmd ResetQueryPool");
        m_impl->ResetQueryPool(pool, firstQuery, queryCount);
    }

    void CommandBuffer::BeginQuery(QueryPool *pool, uint32_t queryIndex, PeQueryControlFlags flags)
    {
        PE_PROFILE_SCOPE("Cmd BeginQuery");
        m_impl->BeginQuery(pool, queryIndex, flags);
    }

    void CommandBuffer::EndQuery(QueryPool *pool, uint32_t queryIndex)
    {
        PE_PROFILE_SCOPE("Cmd EndQuery");
        m_impl->EndQuery(pool, queryIndex);
    }

    void CommandBuffer::WriteTimestamp(QueryPool *pool, uint32_t queryIndex)
    {
        PE_PROFILE_SCOPE("Cmd WriteTimestamp");
        m_impl->WriteTimestamp(pool, queryIndex);
    }

    void CommandBuffer::ResolveQueryPool(QueryPool *pool, uint32_t firstQuery, uint32_t queryCount,
                                         Buffer *dst, uint64_t dstOffset, uint64_t stride, PeQueryResultFlags flags)
    {
        PE_PROFILE_SCOPE("Cmd ResolveQueryPool");
        m_impl->ResolveQueryPool(pool, firstQuery, queryCount, dst, dstOffset, stride, flags);
    }

    void CommandBuffer::TraceRays(uint32_t width, uint32_t height, uint32_t depth)
    {
        PE_PROFILE_SCOPE("Cmd TraceRays");
        m_impl->TraceRays(width, height, depth);
    }

    void CommandBuffer::BufferBarrier(const BufferBarrierInfo &info)
    {
        PE_PROFILE_COUNTER("Cmd BufferBarrier Calls", 1);
        PE_PROFILE_COUNTER("Cmd BufferBarrier Items", 1);
        PE_PROFILE_SCOPE("Cmd BufferBarrier");
        m_impl->BufferBarrier(info);
    }

    void CommandBuffer::BufferBarriers(const std::vector<BufferBarrierInfo> &infos)
    {
        PE_PROFILE_COUNTER("Cmd BufferBarriers Calls", 1);
        PE_PROFILE_COUNTER("Cmd BufferBarrier Items", infos.size());
        PE_PROFILE_SCOPE("Cmd BufferBarriers");
        m_impl->BufferBarriers(infos);
    }

    void CommandBuffer::ImageBarrier(const ImageBarrierInfo &info)
    {
        PE_PROFILE_COUNTER("Cmd ImageBarrier Calls", 1);
        PE_PROFILE_COUNTER("Cmd ImageBarrier Items", 1);
        PE_PROFILE_SCOPE("Cmd ImageBarrier");
        m_impl->ImageBarrier(NormalizeImageBarrierForBackend(info));
    }

    void CommandBuffer::ImageBarriers(const std::vector<ImageBarrierInfo> &infos)
    {
        PE_PROFILE_COUNTER("Cmd ImageBarriers Calls", 1);
        PE_PROFILE_COUNTER("Cmd ImageBarrier Items", infos.size());
        PE_PROFILE_SCOPE("Cmd ImageBarriers");
        if (RHII.GetApi() != PE_GRAPHICS_API_DX12 && !RHII.UsesDozenVulkan())
        {
            m_impl->ImageBarriers(infos);
            return;
        }

        std::vector<ImageBarrierInfo> normalizedInfos;
        normalizedInfos.reserve(infos.size());
        for (const ImageBarrierInfo &info : infos)
            normalizedInfos.push_back(NormalizeImageBarrierForBackend(info));
        m_impl->ImageBarriers(normalizedInfos);
    }

    void CommandBuffer::MemoryBarrier(const MemoryBarrierInfo &info)
    {
        PE_PROFILE_COUNTER("Cmd MemoryBarrier Calls", 1);
        PE_PROFILE_COUNTER("Cmd MemoryBarrier Items", 1);
        PE_PROFILE_SCOPE("Cmd MemoryBarrier");
        m_impl->MemoryBarrier(info);
    }

    void CommandBuffer::MemoryBarriers(const std::vector<MemoryBarrierInfo> &infos)
    {
        PE_PROFILE_COUNTER("Cmd MemoryBarriers Calls", 1);
        PE_PROFILE_COUNTER("Cmd MemoryBarrier Items", infos.size());
        PE_PROFILE_SCOPE("Cmd MemoryBarriers");
        m_impl->MemoryBarriers(infos);
    }

    void CommandBuffer::CopyBuffer(Buffer *src, Buffer *dst, const size_t size, size_t srcOffset, size_t dstOffset)
    {
        m_impl->CopyBuffer(src, dst, size, srcOffset, dstOffset);
    }

    void CommandBuffer::CopyBufferStaged(Buffer *buffer, void *data, size_t size, size_t dstOffset)
    {
        m_impl->CopyBufferStaged(buffer, data, size, dstOffset);
    }

    void CommandBuffer::CopyDataToImageStaged(Image *image,
                                              void *data,
                                              size_t size,
                                              uint32_t baseArrayLayer,
                                              uint32_t layerCount,
                                              uint32_t mipLevel)
    {
        m_impl->CopyDataToImageStaged(image, data, size, baseArrayLayer, layerCount, mipLevel);
    }

    void CommandBuffer::CopyImage(Image *src, Image *dst)
    {
        m_impl->CopyImage(src, dst);
    }

    void CommandBuffer::CopyImageToBuffer(Image *src, Buffer *dst, uint32_t mipLevel, uint32_t baseArrayLayer, uint32_t layerCount)
    {
        m_impl->CopyImageToBuffer(src, dst, mipLevel, baseArrayLayer, layerCount);
    }

    void CommandBuffer::GenerateMipMaps(Image *image)
    {
        m_impl->GenerateMipMaps(image);
    }

    void CommandBuffer::SetEvent(Image *image,
                                 PeImageLayout srcLayout, PeImageLayout dstLayout,
                                 PeBarrierSync srcStage, PeBarrierSync dstStage,
                                 PeBarrierAccess srcAccess, PeBarrierAccess dstAccess)
    {
        PE_ERROR_IF(!m_event, "CommandBuffer::SetEvent: events are not implemented for this backend");
        m_impl->SetEvent(m_event, image, srcLayout, dstLayout, srcStage, dstStage, srcAccess, dstAccess);
    }

    void CommandBuffer::WaitEvent()
    {
        PE_ERROR_IF(!m_event, "CommandBuffer::WaitEvent: events are not implemented for this backend");
        m_event->Wait();
    }

    void CommandBuffer::ResetEvent(PeBarrierSync resetStage)
    {
        PE_ERROR_IF(!m_event, "CommandBuffer::ResetEvent: events are not implemented for this backend");
        m_event->Reset(resetStage);
    }

    void CommandBuffer::BeginDebugRegion(const std::string &name)
    {
        PE_PROFILE_COUNTER("Cmd DebugRegion Begin Calls", 1);
        m_impl->BeginDebugRegion(name);
    }

    void CommandBuffer::InsertDebugLabel(const std::string &name)
    {
        m_impl->InsertDebugLabel(name);
    }

    void CommandBuffer::EndDebugRegion()
    {
        PE_PROFILE_COUNTER("Cmd DebugRegion End Calls", 1);
        m_impl->EndDebugRegion();
    }

    Queue *CommandBuffer::GetQueue() const
    {
        return m_commandPool->GetQueue();
    }

    uint32_t CommandBuffer::GetFamilyId() const
    {
        return GetQueue()->GetFamilyId();
    }

    void CommandBuffer::Wait()
    {
        std::lock_guard<std::mutex> lock(m_WaitMutex);

        PE_ERROR_IF(m_recording, "CommandBuffer::Wait: CommandBuffer is still recording!");

        GetQueue()->GetSubmissionsSemaphore()->Wait(m_submission);

        if (!m_afterWaitCallbacks.IsEmpty())
        {
            m_afterWaitCallbacks.ReverseInvoke();
            m_afterWaitCallbacks.Clear();
        }

#if PE_DEBUG_MODE
        // Pass 1: fetch all GPU timer results and find the frame-start timestamp.
        struct RawResult
        {
            float timeMs;
            double startMs;
        };
        std::vector<RawResult> rawResults;
        rawResults.reserve(m_gpuTimerInfosCount);
        double minStartMs = std::numeric_limits<double>::max();

        for (uint32_t i = 0; i < m_gpuTimerInfosCount; ++i)
        {
            const auto &info = m_gpuTimerInfos[i];
            float timeMs = info.timer ? info.timer->GetTime() : 0.0f;
            double startMs = (info.timer && timeMs > 0.0f) ? info.timer->GetStartTimeMs() : 0.0;
            rawResults.push_back({timeMs, startMs});
            if (startMs > 0.0 && startMs < minStartMs)
                minStartMs = startMs;
        }
        if (minStartMs == std::numeric_limits<double>::max())
            minStartMs = 0.0;

        // Pass 2: build samples with relative start offsets.
        std::vector<GpuTimerSample> samples;
        samples.reserve(m_gpuTimerInfosCount);
        for (uint32_t i = 0; i < m_gpuTimerInfosCount; ++i)
        {
            const auto &info = m_gpuTimerInfos[i];
            GpuTimerSample sample{};
            sample.name = info.name;
            sample.depth = info.depth;
            sample.timeMs = rawResults[i].timeMs;
            sample.startOffsetMs = static_cast<float>(rawResults[i].startMs - minStartMs);
            samples.emplace_back(std::move(sample));
        }

        EventSystem::DispatchEvent(EventType::AfterCommandWait, std::any{std::move(samples)});
        m_gpuTimerInfosCount = 0;
#endif
    }

    void CommandBuffer::Return()
    {
        GetQueue()->ReturnCommandBuffer(this);
    }

    void CommandBuffer::AddAfterWaitCallback(Delegate<>::FunctionType &&func)
    {
        m_afterWaitCallbacks += std::forward<Delegate<>::FunctionType>(func);
    }

    CommandBuffer::Impl *CreateCommandBufferImpl(CommandBuffer *owner, CommandPool *commandPool, const std::string &name)
    {
        if (RHII.GetApi() == PE_GRAPHICS_API_DX12)
        {
#if defined(PE_WIN32)
            return new Dx12CommandBufferImpl(owner, commandPool, name);
#else
            PE_ERROR("[CommandBuffer] DX12 command-buffer creation is Windows-only");
            return nullptr;
#endif
        }

        return new VulkanCommandBufferImpl(owner, commandPool, name);
    }
} // namespace pe
