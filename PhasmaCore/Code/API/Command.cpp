#include "API/Command.h"
#include "API/CommandBuffer_Internal.h"
#include "API/Event.h"
#include "API/Framebuffer.h"
#include "API/Image.h"
#include "API/Pipeline.h"
#include "API/Queue.h"
#include "API/RHI.h"
#include "API/RenderPass.h"
#include "API/Semaphore.h"

namespace pe
{
    CommandBuffer::CommandBuffer(CommandPool *commandPool, const std::string &name)
        : m_id{ID::NextID()},
          m_submission{0},
          m_renderPass{nullptr},
          m_boundPipeline{nullptr},
          m_commandPool{commandPool},
          m_event{Event::Create(name + "_event")},
          m_name{name},
          m_threadId{std::this_thread::get_id()}
    {
        m_impl = CreateCommandBufferImpl(this, commandPool, name);
        PE_ERROR_IF(!m_impl, "CommandBuffer: backend has no command-buffer implementation (DX12 raw bypass should never reach here pre-T10b)");
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
        m_impl->Begin();
    }

    void CommandBuffer::End()
    {
        m_impl->End();
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
            std::string name = "Auto_Gen_RenderPass_" + std::to_string(s_renderPasses.size());
            RenderPass *newRenderPass = RenderPass::Create(count, attachments, name);
            s_renderPasses[hash] = newRenderPass;

            return newRenderPass;
        }
    }

    // Note: Cares only about the actual images, just having the Attachment struct for convenience
    Framebuffer *CommandBuffer::GetFramebuffer(RenderPass *renderPass, uint32_t count, Attachment *attachments)
    {
        Hash hash;
        hash.Combine(reinterpret_cast<std::intptr_t>(renderPass));
        for (uint32_t i = 0; i < count; i++)
            hash.Combine(reinterpret_cast<std::intptr_t>(attachments[i].image));

        auto it = s_framebuffers.find(hash);
        if (it != s_framebuffers.end())
        {
            return it->second;
        }
        else
        {
            std::vector<ImageView *> views{};
            views.reserve(count);

            for (uint32_t i = 0; i < count; i++)
            {
                Image *image = attachments[i].image;
                if (!image->HasRTV())
                    image->CreateRTV();
                views.push_back(image->GetRTV());
            }

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

    void CommandBuffer::BeginPass(uint32_t count, Attachment *attachments, const std::string &name, bool skipDynamicPass)
    {
        m_impl->BeginPass(count, attachments, name, skipDynamicPass);
    }

    void CommandBuffer::EndPass()
    {
        m_impl->EndPass();
    }

    Pipeline *CommandBuffer::GetPipeline(RenderPass *renderPass, PassInfo &passInfo)
    {
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
        m_impl->BindPipeline(passInfo, bindDescriptors);
    }

    void CommandBuffer::BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding, uint32_t bindingCount)
    {
        m_impl->BindVertexBuffer(buffer, offset, firstBinding, bindingCount);
    }

    void CommandBuffer::BindIndexBuffer(Buffer *buffer, size_t offset)
    {
        m_impl->BindIndexBuffer(buffer, offset);
    }

    void CommandBuffer::BindDescriptors(uint32_t count, Descriptor *const *descriptors)
    {
        m_impl->BindDescriptors(count, descriptors);
    }

    void CommandBuffer::PushDescriptor(uint32_t set, const std::vector<PushDescriptorInfo> &info)
    {
        m_impl->PushDescriptor(set, info);
    }

    void CommandBuffer::SetViewport(float x, float y, float width, float height)
    {
        m_impl->SetViewport(x, y, width, height);
    }

    void CommandBuffer::SetScissor(int x, int y, uint32_t width, uint32_t height)
    {
        m_impl->SetScissor(x, y, width, height);
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
        m_impl->Dispatch(groupCountX, groupCountY, groupCountZ);
    }

    void CommandBuffer::PushConstants()
    {
        m_impl->PushConstants(m_pushConstants);
    }

    void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        m_impl->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        m_impl->DrawIndexed(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void CommandBuffer::DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        m_impl->DrawIndirect(indirectBuffer, offset, drawCount, stride);
    }

    void CommandBuffer::DrawIndexedIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        m_impl->DrawIndexedIndirect(indirectBuffer, offset, drawCount, stride);
    }

    void CommandBuffer::DrawIndexedIndirectCount(Buffer *indirectBuffer, size_t offset, Buffer *countBuffer, size_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride)
    {
        m_impl->DrawIndexedIndirectCount(indirectBuffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
    }

    void CommandBuffer::FillBuffer(Buffer *buffer, size_t offset, size_t size, uint32_t data)
    {
        m_impl->FillBuffer(buffer, offset, size, data);
    }

    void CommandBuffer::TraceRays(uint32_t width, uint32_t height, uint32_t depth)
    {
        m_impl->TraceRays(width, height, depth);
    }

    void CommandBuffer::BufferBarrier(const BufferBarrierInfo &info)
    {
        m_impl->BufferBarrier(info);
    }

    void CommandBuffer::BufferBarriers(const std::vector<BufferBarrierInfo> &infos)
    {
        m_impl->BufferBarriers(infos);
    }

    void CommandBuffer::ImageBarrier(const ImageBarrierInfo &info)
    {
        m_impl->ImageBarrier(info);
    }

    void CommandBuffer::ImageBarriers(const std::vector<ImageBarrierInfo> &infos)
    {
        m_impl->ImageBarriers(infos);
    }

    void CommandBuffer::MemoryBarrier(const MemoryBarrierInfo &info)
    {
        m_impl->MemoryBarrier(info);
    }

    void CommandBuffer::MemoryBarriers(const std::vector<MemoryBarrierInfo> &infos)
    {
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

    void CommandBuffer::CopyImageToBuffer(Image *src, Buffer *dst)
    {
        m_impl->CopyImageToBuffer(src, dst);
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
        m_impl->SetEvent(m_event, image, srcLayout, dstLayout, srcStage, dstStage, srcAccess, dstAccess);
    }

    void CommandBuffer::WaitEvent()
    {
        m_event->Wait();
    }

    void CommandBuffer::ResetEvent(PeBarrierSync resetStage)
    {
        m_event->Reset(resetStage);
    }

    void CommandBuffer::BeginDebugRegion(const std::string &name)
    {
        m_impl->BeginDebugRegion(name);
    }

    void CommandBuffer::InsertDebugLabel(const std::string &name)
    {
        m_impl->InsertDebugLabel(name);
    }

    void CommandBuffer::EndDebugRegion()
    {
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
} // namespace pe
