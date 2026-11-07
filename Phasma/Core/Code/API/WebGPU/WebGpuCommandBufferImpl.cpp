#include "API/WebGPU/WebGpuCommandBufferImpl.h"
#include "API/Buffer.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/ImageView.h"
#include "API/Pipeline.h"
#include "API/RHI.h"
#include "API/StagingManager.h"
#include "API/WebGPU/WebGpuBufferImpl.h"
#include "API/WebGPU/WebGpuDescriptorImpl.h"
#include "API/WebGPU/WebGpuImageImpl.h"
#include "API/WebGPU/WebGpuPipelineImpl.h"
#include "API/WebGPU/WebGpuRhiImpl.h"

namespace pe
{
    namespace
    {
        // ponytail: queries (occlusion, timestamps) are off in the first web version (decision 5); they fail loudly at
        // first use instead of recording nothing.
        void NotYet(const char *what)
        {
            PE_ERROR("WebGPU backend: %s is not implemented yet", what);
        }

        bool Aligned4(size_t v)
        {
            return (v & 3) == 0;
        }

        // WebGPU has no "don't care" load: clearing is the cheapest defined start.
        WGPULoadOp ToLoadOp(PeLoadOp op)
        {
            return op == PE_LOAD_OP_LOAD ? WGPULoadOp_Load : WGPULoadOp_Clear;
        }

        WGPUStoreOp ToStoreOp(PeStoreOp op)
        {
            return op == PE_STORE_OP_STORE ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
        }

        WGPUStringView View(const std::string &text)
        {
            return {text.data(), text.size()};
        }

        WGPUTextureView AttachmentView(Image *image, ImageView *view, const char *what)
        {
            PE_ERROR_IF(!image, "WebGpuCommandBufferImpl::%s: null attachment image", what);
            if (!view)
                view = image->GetRTV();

            PE_ERROR_IF(!view, "WebGpuCommandBufferImpl::%s: '%s' has no render-target view", what, image->GetName().c_str());
            WGPUTextureView handle = WebGpuImageViewImpl::From(view)->m_view;
            PE_ERROR_IF(!handle, "WebGpuCommandBufferImpl::%s: '%s' has no texture yet", what, image->GetName().c_str());
            return handle;
        }

        WGPURenderPassDepthStencilAttachment DepthAttachment(const Attachment &att, WGPUTextureView view)
        {
            const vec4 clear = att.image->GetClearColor();
            WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            depth.view = view;
            depth.depthLoadOp = ToLoadOp(att.loadOp);
            depth.depthStoreOp = ToStoreOp(att.storeOp);
            depth.depthClearValue = clear.x;
            if (!PeFormatHasStencil(att.image->GetFormat()))
                return depth; // stencil ops must stay undefined without a stencil aspect

            depth.stencilLoadOp = ToLoadOp(att.stencilLoadOp);
            depth.stencilStoreOp = ToStoreOp(att.stencilStoreOp);
            depth.stencilClearValue = static_cast<uint32_t>(clear.y);
            return depth;
        }
    } // namespace

    WebGpuCommandBufferImpl::WebGpuCommandBufferImpl(CommandBuffer *owner, CommandPool *, const std::string &)
        : m_owner{owner}
    {
    }

    WebGpuCommandBufferImpl::~WebGpuCommandBufferImpl()
    {
        Release();
    }

    void WebGpuCommandBufferImpl::Release()
    {
        if (m_renderPass)
            wgpuRenderPassEncoderRelease(m_renderPass);

        if (m_computePass)
            wgpuComputePassEncoderRelease(m_computePass);

        if (m_encoder)
            wgpuCommandEncoderRelease(m_encoder);

        if (m_commands)
            wgpuCommandBufferRelease(m_commands);

        m_renderPass = nullptr;
        m_computePass = nullptr;
        m_encoder = nullptr;
        m_commands = nullptr;
        m_referenced.clear();
        m_boundDescriptors.clear();
        m_vertexBuffers.clear();
        m_indexBuffer = nullptr;
        m_pushOffset = 0;
        m_hasViewport = false;
        m_hasScissor = false;
        m_hasBlendConstant = false;
        m_hasStencilReference = false;
        m_debugDepth = 0;
        m_passDebugDepth = 0;
        m_deferredPops = 0;
        ResetPassState();
    }

    void WebGpuCommandBufferImpl::Begin()
    {
        Release();
        m_encoder = wgpuDeviceCreateCommandEncoder(WebGpuRhiImpl::Get()->m_device, nullptr);
        PE_ERROR_IF(!m_encoder, "WebGpuCommandBufferImpl::Begin: failed to create a command encoder");
    }

    void WebGpuCommandBufferImpl::End()
    {
        WGPUCommandEncoder encoder = Encoder("End");
        for (; m_debugDepth > 0; --m_debugDepth)
            wgpuCommandEncoderPopDebugGroup(encoder);

        m_commands = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuCommandEncoderRelease(m_encoder);
        m_encoder = nullptr;
    }

    void WebGpuCommandBufferImpl::Reset()
    {
        Release();
    }

    WGPUCommandEncoder WebGpuCommandBufferImpl::Encoder(const char *what)
    {
        PE_ERROR_IF(!m_encoder, "WebGpuCommandBufferImpl: %s outside a recording", what);
        PE_ERROR_IF(m_renderPass, "WebGpuCommandBufferImpl: %s inside a render pass", what);
        EndComputePass();
        return m_encoder;
    }

    void WebGpuCommandBufferImpl::EndComputePass()
    {
        if (!m_computePass)
            return;

        wgpuComputePassEncoderEnd(m_computePass);
        wgpuComputePassEncoderRelease(m_computePass);
        m_computePass = nullptr;
    }

    void WebGpuCommandBufferImpl::ResetPassState()
    {
        m_appliedPipeline = nullptr;
        std::fill(std::begin(m_appliedGroups), std::end(m_appliedGroups), nullptr);
        m_appliedPushOffset = UINT32_MAX;
        m_dynamicDirty = true;
    }

    void WebGpuCommandBufferImpl::ReferenceBuffer(Buffer *buffer)
    {
        WebGpuBufferImpl *impl = WebGpuBufferImpl::From(buffer);
        if (impl->UploadsWhenReferenced())
            m_referenced.push_back(impl);
    }

    void WebGpuCommandBufferImpl::UploadReferencedBuffers()
    {
        std::sort(m_referenced.begin(), m_referenced.end());
        m_referenced.erase(std::unique(m_referenced.begin(), m_referenced.end()), m_referenced.end());

        // Rule 2: a referenced buffer that is still mapped may hold raw-pointer writes no Flush or Unmap uploaded.
        for (WebGpuBufferImpl *impl : m_referenced)
        {
            if (impl->IsMapped())
                impl->UploadShadow();
        }

        m_referenced.clear();
    }

    WGPUCommandBuffer WebGpuCommandBufferImpl::TakeCommands()
    {
        WGPUCommandBuffer commands = m_commands;
        m_commands = nullptr;
        return commands;
    }

    void WebGpuCommandBufferImpl::CheckNotGpuWritten(Buffer *buffer, const char *what)
    {
        // Rule 3: the GPU's write would be overwritten by the buffer's shadow at the next upload.
        PE_ERROR_IF(WebGpuBufferImpl::From(buffer)->IsHostVisible(),
                    "%s: '%s' is host-visible, and WebGPU host-visible buffers are never GPU-written",
                    what,
                    buffer->GetName().c_str());
    }

    void WebGpuCommandBufferImpl::FillBuffer(Buffer *buffer, size_t offset, size_t size, uint32_t data)
    {
        PE_ERROR_IF(!buffer, "WebGpuCommandBufferImpl::FillBuffer: null buffer");
        PE_ERROR_IF(offset + size > buffer->Size(), "WebGpuCommandBufferImpl::FillBuffer: range overflow");
        if (!size)
            return;

        CheckNotGpuWritten(buffer, "WebGpuCommandBufferImpl::FillBuffer");
        PE_ERROR_IF(!Aligned4(offset) || !Aligned4(size), "WebGpuCommandBufferImpl::FillBuffer: WebGPU needs 4-byte offsets and sizes");

        if (data == 0)
        {
            wgpuCommandEncoderClearBuffer(Encoder("FillBuffer"), WebGpuBufferImpl::From(buffer)->m_buffer, offset, size);
            return;
        }

        // A non-zero pattern goes through staging; it is never reused within this submit.
        StagingAllocation alloc = RHII.GetStagingManager()->Allocate(size);
        auto *words = static_cast<uint32_t *>(alloc.data);
        std::fill(words, words + size / 4, data);
        alloc.buffer->Flush(size, 0);

        CopyBuffer(alloc.buffer, buffer, size, 0, offset);
        m_owner->AddAfterWaitCallback([alloc = std::move(alloc)]()
                                      { RHII.GetStagingManager()->SetUnused(alloc); });
    }

    void WebGpuCommandBufferImpl::CopyBuffer(Buffer *src, Buffer *dst, size_t size, size_t srcOffset, size_t dstOffset)
    {
        PE_ERROR_IF(!src || !dst, "WebGpuCommandBufferImpl::CopyBuffer: null buffer");
        PE_ERROR_IF(srcOffset + size > src->Size(), "WebGpuCommandBufferImpl::CopyBuffer: source range overflow");
        PE_ERROR_IF(dstOffset + size > dst->Size(), "WebGpuCommandBufferImpl::CopyBuffer: destination range overflow");
        if (!size)
            return;

        CheckNotGpuWritten(dst, "WebGpuCommandBufferImpl::CopyBuffer");

        // ponytail: WebGPU copies need 4-byte offsets and sizes. Rounding the size up would overwrite up to 3 live
        // bytes after the destination range, so misaligned copies fail here; if a caller needs them, copy the
        // aligned body and write the tail through a staged read-modify of the last word.
        const bool aligned = Aligned4(srcOffset) && Aligned4(dstOffset) && Aligned4(size);
        PE_ERROR_IF(!aligned,
                    "WebGpuCommandBufferImpl::CopyBuffer: '%s' -> '%s' is not 4-byte aligned (src %zu, dst %zu, size %zu)",
                    src->GetName().c_str(),
                    dst->GetName().c_str(),
                    srcOffset,
                    dstOffset,
                    size);

        ReferenceBuffer(src);
        wgpuCommandEncoderCopyBufferToBuffer(Encoder("CopyBuffer"),
                                             WebGpuBufferImpl::From(src)->m_buffer,
                                             srcOffset,
                                             WebGpuBufferImpl::From(dst)->m_buffer,
                                             dstOffset,
                                             size);
    }

    void WebGpuCommandBufferImpl::CopyBufferStaged(Buffer *buffer, void *data, size_t size, size_t dstOffset)
    {
        PE_ERROR_IF(!buffer, "WebGpuCommandBufferImpl::CopyBufferStaged: null buffer");
        buffer->CopyBufferStaged(m_owner, data, size, dstOffset);
    }

    void WebGpuCommandBufferImpl::BeginDebugRegion(const std::string &name)
    {
        if (m_renderPass)
        {
            wgpuRenderPassEncoderPushDebugGroup(m_renderPass, View(name));
            m_passDebugDepth++;
            return;
        }

        wgpuCommandEncoderPushDebugGroup(Encoder("BeginDebugRegion"), View(name));
        m_debugDepth++;
    }

    void WebGpuCommandBufferImpl::InsertDebugLabel(const std::string &name)
    {
        if (m_renderPass)
            wgpuRenderPassEncoderInsertDebugMarker(m_renderPass, View(name));
        else if (m_computePass)
            wgpuComputePassEncoderInsertDebugMarker(m_computePass, View(name));
        else
            wgpuCommandEncoderInsertDebugMarker(Encoder("InsertDebugLabel"), View(name));
    }

    void WebGpuCommandBufferImpl::EndDebugRegion()
    {
        if (m_renderPass)
        {
            if (m_passDebugDepth > 0)
            {
                wgpuRenderPassEncoderPopDebugGroup(m_renderPass);
                m_passDebugDepth--;
            }
            else if (m_deferredPops < m_debugDepth)
            {
                m_deferredPops++; // opened before this pass; the command encoder is locked until it ends
            }

            return;
        }

        // CommandBuffer::End always closes a region, opened or not.
        if (m_debugDepth == 0)
            return;

        wgpuCommandEncoderPopDebugGroup(Encoder("EndDebugRegion"));
        m_debugDepth--;
    }

    void WebGpuCommandBufferImpl::BlitImage(Image *src, Image *dst, const ImageBlit &region, PeFilter filter)
    {
        PE_ERROR_IF(!src || !dst, "WebGpuCommandBufferImpl::BlitImage: null image");
        dst->Blit(m_owner, src, region, filter);
    }

    void WebGpuCommandBufferImpl::ClearColors(std::vector<Image *> images)
    {
        // A clear outside a pass is an empty render pass with loadOp = Clear (decision 5).
        WGPUCommandEncoder encoder = Encoder("ClearColors");
        for (Image *image : images)
        {
            WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            color.view = AttachmentView(image, nullptr, "ClearColors");
            color.loadOp = WGPULoadOp_Clear;
            color.storeOp = WGPUStoreOp_Store;
            const vec4 clear = image->GetClearColor();
            color.clearValue = {clear.x, clear.y, clear.z, clear.w};

            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &color;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
    }

    void WebGpuCommandBufferImpl::ClearDepthStencils(std::vector<Image *> images)
    {
        WGPUCommandEncoder encoder = Encoder("ClearDepthStencils");
        for (Image *image : images)
        {
            PE_ERROR_IF(!image || !PeFormatHasDepthOrStencil(image->GetFormat()),
                        "WebGpuCommandBufferImpl::ClearDepthStencils: not a depth/stencil image");

            Attachment att{};
            att.image = image;
            att.loadOp = PE_LOAD_OP_CLEAR;
            att.stencilLoadOp = PE_LOAD_OP_CLEAR;
            att.stencilStoreOp = PE_STORE_OP_STORE;
            WGPURenderPassDepthStencilAttachment depth = DepthAttachment(att, AttachmentView(image, nullptr, "ClearDepthStencils"));

            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.depthStencilAttachment = &depth;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
    }

    void WebGpuCommandBufferImpl::BeginPass(uint32_t count, Attachment *attachments, const std::string &name, bool)
    {
        PE_ERROR_IF(count > 0 && !attachments, "WebGpuCommandBufferImpl::BeginPass: null attachments");
        WGPUCommandEncoder encoder = Encoder("BeginPass");

        m_owner->m_dynamicPass = true;
        m_owner->m_attachmentCount = count;
        m_owner->m_attachments = attachments;

        std::vector<WGPURenderPassColorAttachment> colors;
        WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        bool hasDepth = false;
        for (uint32_t i = 0; i < count; ++i)
        {
            const Attachment &att = attachments[i];
            const WGPUTextureView view = AttachmentView(att.image, att.view, "BeginPass");
            const uint32_t mip = att.view ? att.view->GetDesc().baseMipLevel : 0;
            m_attachmentWidth = std::max(1u, att.image->GetWidth() >> mip);
            m_attachmentHeight = std::max(1u, att.image->GetHeight() >> mip);
            if (PeFormatHasDepthOrStencil(att.image->GetFormat()))
            {
                PE_ERROR_IF(hasDepth, "WebGpuCommandBufferImpl::BeginPass: more than one depth/stencil attachment");
                depth = DepthAttachment(att, view);
                hasDepth = true;
                continue;
            }

            const vec4 clear = att.image->GetClearColor();
            WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            color.view = view;
            color.loadOp = ToLoadOp(att.loadOp);
            color.storeOp = ToStoreOp(att.storeOp);
            color.clearValue = {clear.x, clear.y, clear.z, clear.w};
            colors.push_back(color);
        }

        WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        desc.label = View(name);
        desc.colorAttachmentCount = colors.size();
        desc.colorAttachments = colors.data();
        desc.depthStencilAttachment = hasDepth ? &depth : nullptr;
        m_renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
        ResetPassState();
    }

    void WebGpuCommandBufferImpl::EndPass()
    {
        PE_ERROR_IF(!m_renderPass, "WebGpuCommandBufferImpl::EndPass: no render pass is open");
        for (; m_passDebugDepth > 0; --m_passDebugDepth)
            wgpuRenderPassEncoderPopDebugGroup(m_renderPass);

        wgpuRenderPassEncoderEnd(m_renderPass);
        wgpuRenderPassEncoderRelease(m_renderPass);
        m_renderPass = nullptr;

        for (; m_deferredPops > 0; --m_deferredPops)
        {
            wgpuCommandEncoderPopDebugGroup(m_encoder);
            m_debugDepth--;
        }

        m_owner->FixupLoadOpAttachmentWriteAccess();
        m_owner->ClearBoundGraphicsState();
    }

    void WebGpuCommandBufferImpl::BindPipeline(PassInfo &passInfo, bool bindDescriptors)
    {
        Pipeline *pipeline = CommandBuffer::GetPipeline(m_owner->m_renderPass, passInfo);
        PE_ERROR_IF(!pipeline, "WebGpuCommandBufferImpl::BindPipeline: pipeline creation failed");
        m_owner->m_boundPipeline = pipeline;
        if (!bindDescriptors)
            return;

        const std::vector<Descriptor *> &descriptors = passInfo.GetDescriptors(RHII.GetFrameIndex());
        BindDescriptors(static_cast<uint32_t>(descriptors.size()), descriptors.data());
    }

    void WebGpuCommandBufferImpl::BindVertexBuffer(Buffer *buffer, size_t offset, uint32_t firstBinding, uint32_t bindingCount)
    {
        PE_ERROR_IF(!buffer, "WebGpuCommandBufferImpl::BindVertexBuffer: null buffer");
        PE_ERROR_IF(offset > buffer->Size(), "WebGpuCommandBufferImpl::BindVertexBuffer: offset exceeds buffer size");
        if (m_vertexBuffers.size() < firstBinding + bindingCount)
            m_vertexBuffers.resize(firstBinding + bindingCount);

        for (uint32_t i = 0; i < bindingCount; ++i)
            m_vertexBuffers[firstBinding + i] = {buffer, offset};

        ReferenceBuffer(buffer);
        m_dynamicDirty = true;
    }

    void WebGpuCommandBufferImpl::BindIndexBuffer(Buffer *buffer, size_t offset, PeIndexType indexType)
    {
        PE_ERROR_IF(!buffer, "WebGpuCommandBufferImpl::BindIndexBuffer: null buffer");
        PE_ERROR_IF(offset > buffer->Size(), "WebGpuCommandBufferImpl::BindIndexBuffer: offset exceeds buffer size");
        m_indexBuffer = buffer;
        m_indexOffset = offset;
        m_indexFormat = indexType == PE_INDEX_TYPE_UINT16 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
        ReferenceBuffer(buffer);
        m_dynamicDirty = true;
    }

    void WebGpuCommandBufferImpl::BindDescriptors(uint32_t count, Descriptor *const *descriptors)
    {
        // Sets 0..count-1, as vkCmdBindDescriptorSets with firstSet 0: higher sets and null entries stay bound.
        if (m_boundDescriptors.size() < count)
            m_boundDescriptors.resize(count, nullptr);

        for (uint32_t i = 0; i < count; ++i)
        {
            if (!descriptors[i])
                continue;

            m_boundDescriptors[i] = descriptors[i];

            // Rule 2: a submit that binds a group uploads the mapped buffers in it.
            for (Buffer *buffer : WebGpuDescriptorImpl::From(descriptors[i])->GetReferencedBuffers())
                ReferenceBuffer(buffer);
        }
    }

    void WebGpuCommandBufferImpl::PushDescriptor(uint32_t, const std::vector<PushDescriptorInfo> &)
    {
        PE_ERROR("WebGpuCommandBufferImpl::PushDescriptor: not supported on WebGPU");
    }

    void WebGpuCommandBufferImpl::SetViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
        const float viewport[6] = {x, y, width, height, minDepth, maxDepth};
        std::copy(std::begin(viewport), std::end(viewport), m_viewport);
        m_hasViewport = true;
        m_dynamicDirty = true;
    }

    void WebGpuCommandBufferImpl::SetScissor(int x, int y, uint32_t width, uint32_t height)
    {
        m_scissorX = x;
        m_scissorY = y;
        m_scissorWidth = width;
        m_scissorHeight = height;
        m_hasScissor = true;
        m_dynamicDirty = true;
    }

    void WebGpuCommandBufferImpl::SetBlendConstants(const float constants[4])
    {
        m_blendConstant = {constants[0], constants[1], constants[2], constants[3]};
        m_hasBlendConstant = true;
        m_dynamicDirty = true;
    }

    void WebGpuCommandBufferImpl::SetStencilReference(uint32_t reference)
    {
        m_stencilReference = reference;
        m_hasStencilReference = true;
        m_dynamicDirty = true;
    }

    WebGpuPipelineImpl *WebGpuCommandBufferImpl::BoundPipeline(bool compute, const char *what)
    {
        Pipeline *pipeline = m_owner->m_boundPipeline;
        PE_ERROR_IF(!pipeline, "WebGpuCommandBufferImpl::%s: no bound pipeline", what);
        WebGpuPipelineImpl *impl = WebGpuPipelineImpl::From(pipeline);
        PE_ERROR_IF(compute ? !impl->m_compute : !impl->m_render,
                    "WebGpuCommandBufferImpl::%s: the bound pipeline '%s' is not a %s pipeline",
                    what,
                    pipeline->GetInfo().name.c_str(),
                    compute ? "compute" : "render");
        return impl;
    }

    WGPUBindGroup WebGpuCommandBufferImpl::GroupAt(uint32_t index, const WebGpuPipelineImpl *pipeline)
    {
        if (pipeline->m_emptyGroups & (1u << index))
            return WebGpuRhiImpl::Get()->EmptyGroup();

        Descriptor *descriptor = index < m_boundDescriptors.size() ? m_boundDescriptors[index] : nullptr;
        PE_ERROR_IF(!descriptor, "WebGpuCommandBufferImpl: the bound pipeline needs set %u, and none is bound", index);
        return WebGpuDescriptorImpl::From(descriptor)->GetBindGroup();
    }

    void WebGpuCommandBufferImpl::FlushRenderState(const char *what)
    {
        PE_ERROR_IF(!m_renderPass, "WebGpuCommandBufferImpl::%s: outside a render pass", what);
        WebGpuPipelineImpl *pipeline = BoundPipeline(false, what);
        if (m_owner->m_boundPipeline != m_appliedPipeline)
        {
            wgpuRenderPassEncoderSetPipeline(m_renderPass, pipeline->m_render);
            m_appliedPipeline = m_owner->m_boundPipeline;
        }

        for (uint32_t i = 0; i < WebGpuRhiImpl::kPushGroup; ++i)
        {
            const WGPUBindGroup group = GroupAt(i, pipeline);
            if (group == m_appliedGroups[i])
                continue;

            wgpuRenderPassEncoderSetBindGroup(m_renderPass, i, group, 0, nullptr);
            m_appliedGroups[i] = group;
        }

        if (m_pushOffset != m_appliedPushOffset)
        {
            WGPUBindGroup push = WebGpuRhiImpl::Get()->PushGroup();
            wgpuRenderPassEncoderSetBindGroup(m_renderPass, WebGpuRhiImpl::kPushGroup, push, 1, &m_pushOffset);
            m_appliedPushOffset = m_pushOffset;
        }

        if (m_dynamicDirty)
            FlushDynamicState();
    }

    void WebGpuCommandBufferImpl::FlushDynamicState()
    {
        for (uint32_t i = 0; i < m_vertexBuffers.size(); ++i)
        {
            const VertexBinding &binding = m_vertexBuffers[i];
            if (binding.buffer)
            {
                WGPUBuffer buffer = WebGpuBufferImpl::From(binding.buffer)->m_buffer;
                wgpuRenderPassEncoderSetVertexBuffer(m_renderPass, i, buffer, binding.offset, WGPU_WHOLE_SIZE);
            }
        }

        if (m_indexBuffer)
        {
            WGPUBuffer buffer = WebGpuBufferImpl::From(m_indexBuffer)->m_buffer;
            wgpuRenderPassEncoderSetIndexBuffer(m_renderPass, buffer, m_indexFormat, m_indexOffset, WGPU_WHOLE_SIZE);
        }

        if (m_hasViewport)
        {
            const float *v = m_viewport;
            wgpuRenderPassEncoderSetViewport(m_renderPass, v[0], v[1], v[2], v[3], v[4], v[5]);
        }

        if (m_hasScissor)
            FlushScissor();

        if (m_hasBlendConstant)
            wgpuRenderPassEncoderSetBlendConstant(m_renderPass, &m_blendConstant);

        if (m_hasStencilReference)
            wgpuRenderPassEncoderSetStencilReference(m_renderPass, m_stencilReference);

        m_dynamicDirty = false;
    }

    // WebGPU rejects a scissor that leaves the attachment (setScissorRect); Vulkan and DX12 clip it.
    void WebGpuCommandBufferImpl::FlushScissor()
    {
        const int64_t width = m_attachmentWidth;
        const int64_t height = m_attachmentHeight;
        const int64_t x0 = std::clamp<int64_t>(m_scissorX, 0, width);
        const int64_t y0 = std::clamp<int64_t>(m_scissorY, 0, height);
        const int64_t x1 = std::clamp<int64_t>(static_cast<int64_t>(m_scissorX) + m_scissorWidth, x0, width);
        const int64_t y1 = std::clamp<int64_t>(static_cast<int64_t>(m_scissorY) + m_scissorHeight, y0, height);
        wgpuRenderPassEncoderSetScissorRect(m_renderPass,
                                            static_cast<uint32_t>(x0),
                                            static_cast<uint32_t>(y0),
                                            static_cast<uint32_t>(x1 - x0),
                                            static_cast<uint32_t>(y1 - y0));
    }

    void WebGpuCommandBufferImpl::FlushComputeState(const char *what)
    {
        PE_ERROR_IF(!m_encoder, "WebGpuCommandBufferImpl::%s: outside a recording", what);
        PE_ERROR_IF(m_renderPass, "WebGpuCommandBufferImpl::%s: inside a render pass", what);
        WebGpuPipelineImpl *pipeline = BoundPipeline(true, what);
        if (!m_computePass)
        {
            m_computePass = wgpuCommandEncoderBeginComputePass(m_encoder, nullptr);
            ResetPassState();
        }

        if (m_owner->m_boundPipeline != m_appliedPipeline)
        {
            wgpuComputePassEncoderSetPipeline(m_computePass, pipeline->m_compute);
            m_appliedPipeline = m_owner->m_boundPipeline;
        }

        for (uint32_t i = 0; i < WebGpuRhiImpl::kPushGroup; ++i)
        {
            const WGPUBindGroup group = GroupAt(i, pipeline);
            if (group == m_appliedGroups[i])
                continue;

            wgpuComputePassEncoderSetBindGroup(m_computePass, i, group, 0, nullptr);
            m_appliedGroups[i] = group;
        }

        if (m_pushOffset != m_appliedPushOffset)
        {
            WGPUBindGroup push = WebGpuRhiImpl::Get()->PushGroup();
            wgpuComputePassEncoderSetBindGroup(m_computePass, WebGpuRhiImpl::kPushGroup, push, 1, &m_pushOffset);
            m_appliedPushOffset = m_pushOffset;
        }
    }

    void WebGpuCommandBufferImpl::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        FlushComputeState("Dispatch");
        wgpuComputePassEncoderDispatchWorkgroups(m_computePass, groupCountX, groupCountY, groupCountZ);
    }

    void WebGpuCommandBufferImpl::DispatchIndirect(Buffer *indirectBuffer, size_t offset)
    {
        PE_ERROR_IF(!indirectBuffer, "WebGpuCommandBufferImpl::DispatchIndirect: null indirect buffer");
        PE_ERROR_IF(!Aligned4(offset), "WebGpuCommandBufferImpl::DispatchIndirect: offset is not 4-byte aligned");
        ReferenceBuffer(indirectBuffer);
        FlushComputeState("DispatchIndirect");
        WGPUBuffer buffer = WebGpuBufferImpl::From(indirectBuffer)->m_buffer;
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(m_computePass, buffer, offset);
    }

    // The whole 128-byte block goes into a fresh ring slot; the next draw or dispatch binds group 3 at its offset.
    void WebGpuCommandBufferImpl::PushConstants(const PushConstantsBlock<128> &constants)
    {
        m_pushOffset = WebGpuRhiImpl::Get()->WritePush(constants.Data());
    }

    void WebGpuCommandBufferImpl::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        FlushRenderState("Draw");
        wgpuRenderPassEncoderDraw(m_renderPass, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void WebGpuCommandBufferImpl::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        PE_ERROR_IF(!m_indexBuffer, "WebGpuCommandBufferImpl::DrawIndexed: no index buffer bound");
        FlushRenderState("DrawIndexed");
        wgpuRenderPassEncoderDrawIndexed(m_renderPass,
                                         indexCount,
                                         instanceCount,
                                         firstIndex,
                                         vertexOffset,
                                         firstInstance);
    }

    // WebGPU draws one command per indirect call, so drawCount becomes a loop (decision 5).
    void WebGpuCommandBufferImpl::DrawIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        PE_ERROR_IF(!indirectBuffer, "WebGpuCommandBufferImpl::DrawIndirect: null indirect buffer");
        PE_ERROR_IF(!Aligned4(offset) || !Aligned4(stride), "WebGpuCommandBufferImpl::DrawIndirect: offset or stride not 4-byte aligned");
        if (drawCount == 0)
            return;

        ReferenceBuffer(indirectBuffer);
        FlushRenderState("DrawIndirect");
        WGPUBuffer buffer = WebGpuBufferImpl::From(indirectBuffer)->m_buffer;
        for (uint32_t i = 0; i < drawCount; ++i)
            wgpuRenderPassEncoderDrawIndirect(m_renderPass, buffer, offset + static_cast<uint64_t>(i) * stride);
    }

    void WebGpuCommandBufferImpl::DrawIndexedIndirect(Buffer *indirectBuffer, size_t offset, uint32_t drawCount, uint32_t stride)
    {
        PE_ERROR_IF(!indirectBuffer, "WebGpuCommandBufferImpl::DrawIndexedIndirect: null indirect buffer");
        PE_ERROR_IF(!m_indexBuffer, "WebGpuCommandBufferImpl::DrawIndexedIndirect: no index buffer bound");
        PE_ERROR_IF(!Aligned4(offset) || !Aligned4(stride), "WebGpuCommandBufferImpl::DrawIndexedIndirect: offset or stride not 4-byte aligned");
        if (drawCount == 0)
            return;

        ReferenceBuffer(indirectBuffer);
        FlushRenderState("DrawIndexedIndirect");
        WGPUBuffer buffer = WebGpuBufferImpl::From(indirectBuffer)->m_buffer;
        for (uint32_t i = 0; i < drawCount; ++i)
            wgpuRenderPassEncoderDrawIndexedIndirect(m_renderPass, buffer, offset + static_cast<uint64_t>(i) * stride);
    }

    void WebGpuCommandBufferImpl::DrawIndexedIndirectCount(Buffer *, size_t, Buffer *, size_t, uint32_t, uint32_t)
    {
        PE_ERROR("WebGpuCommandBufferImpl::DrawIndexedIndirectCount: WebGPU has no indirect count (caps.indirectCount is false)");
    }

    void WebGpuCommandBufferImpl::CopyDataToImageStaged(Image *image,
                                                        void *data,
                                                        size_t size,
                                                        uint32_t baseArrayLayer,
                                                        uint32_t layerCount,
                                                        uint32_t mipLevel)
    {
        PE_ERROR_IF(!image, "WebGpuCommandBufferImpl::CopyDataToImageStaged: null image");
        image->CopyDataToImageStaged(m_owner, data, size, baseArrayLayer, layerCount, mipLevel);
    }

    void WebGpuCommandBufferImpl::CopyImage(Image *src, Image *dst)
    {
        PE_ERROR_IF(!src || !dst, "WebGpuCommandBufferImpl::CopyImage: null image");
        dst->CopyImage(m_owner, src);
    }

    void WebGpuCommandBufferImpl::CopyImageToBuffer(Image *src, Buffer *dst, uint32_t mipLevel, uint32_t baseArrayLayer, uint32_t layerCount)
    {
        PE_ERROR_IF(!src || !dst, "WebGpuCommandBufferImpl::CopyImageToBuffer: null image or buffer");
        src->CopyToBuffer(m_owner, dst, mipLevel, baseArrayLayer, layerCount);
    }

    void WebGpuCommandBufferImpl::GenerateMipMaps(Image *image)
    {
        PE_ERROR_IF(!image, "WebGpuCommandBufferImpl::GenerateMipMaps: null image");
        WebGpuImageImpl::From(image)->GenerateMipMaps(m_owner);
    }

    void WebGpuCommandBufferImpl::ResetQueryPool(QueryPool *, uint32_t, uint32_t)
    {
        NotYet("ResetQueryPool");
    }

    void WebGpuCommandBufferImpl::BeginQuery(QueryPool *, uint32_t, PeQueryControlFlags)
    {
        NotYet("BeginQuery");
    }

    void WebGpuCommandBufferImpl::EndQuery(QueryPool *, uint32_t)
    {
        NotYet("EndQuery");
    }

    void WebGpuCommandBufferImpl::WriteTimestamp(QueryPool *, uint32_t)
    {
        NotYet("WriteTimestamp");
    }

    void WebGpuCommandBufferImpl::ResolveQueryPool(QueryPool *, uint32_t, uint32_t, Buffer *, uint64_t, uint64_t, PeQueryResultFlags)
    {
        NotYet("ResolveQueryPool");
    }

    void WebGpuCommandBufferImpl::TraceRays(uint32_t, uint32_t, uint32_t)
    {
        PE_ERROR("WebGpuCommandBufferImpl::TraceRays: WebGPU has no ray tracing");
    }

    void WebGpuCommandBufferImpl::SetEvent(Event *, Image *, PeImageLayout, PeImageLayout, PeBarrierSync, PeBarrierSync, PeBarrierAccess, PeBarrierAccess)
    {
        PE_ERROR("WebGpuCommandBufferImpl::SetEvent: not supported on WebGPU");
    }
} // namespace pe
