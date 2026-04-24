#include "RenderPass.h"
#include "CommandEncoder.h"
#include "RenderPipeline.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "Texture.h"
#include "RenderBundle.h"
#include "QuerySet.h"
#include "Device.h"
#include "Utils.h"

extern "C" void wgpuRenderPipelineAddRef(WGPURenderPipeline);
extern "C" void wgpuRenderPipelineRelease(WGPURenderPipeline);
extern "C" void wgpuBindGroupAddRef(WGPUBindGroup);
extern "C" void wgpuBindGroupRelease(WGPUBindGroup);
extern "C" void wgpuTextureViewRelease(WGPUTextureView);
extern "C" void wgpuQuerySetRelease(WGPUQuerySet);
extern "C" void wgpuRenderBundleAddRef(WGPURenderBundle);
extern "C" void wgpuRenderBundleRelease(WGPURenderBundle);

namespace
{
    // §13.7: encoder errors fire at finish() (first wins, bubbled by pass.end).
    void ReportPassValidation(WGPURenderPassEncoder rpe, const char *msg)
    {
        if (!rpe)
            return;
        if (rpe->deferredErrorMessage.empty())
            rpe->deferredErrorMessage = msg ? msg : "";
    }

    bool ValidateBindGroupCompat(WGPURenderPassEncoder rpe)
    {
        if (!rpe || !rpe->pipeline || !rpe->pipeline->layout)
            return true;
        auto &bgls = rpe->pipeline->layout->bindGroupLayouts;
        for (size_t i = 0; i < bgls.size(); ++i)
        {
            auto *plBgl = bgls[i];
            if (!plBgl)
                continue;
            // §10.2.7: empty default/explicit BGLs are treated as "null" and
            // ignored when checking setBindGroup() compatibility.
            if (plBgl->entries.empty())
                continue;
            if (i >= rpe->currentBindGroups.size())
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "render pass: bind group at index %zu required by pipeline "
                              "layout is not set",
                              i);
                ReportPassValidation(rpe, buf);
                rpe->invalid = true;
                return false;
            }
            auto *bg = rpe->currentBindGroups[i];
            if (!bg || bg->invalid || !bg->layout)
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "render pass: bind group at index %zu required by pipeline "
                              "layout is null or invalid",
                              i);
                ReportPassValidation(rpe, buf);
                rpe->invalid = true;
                return false;
            }
            if (!BglGroupEquivalent(bg->layout, plBgl))
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "render pass: bind group at index %zu is not group-equivalent "
                              "with the pipeline layout",
                              i);
                ReportPassValidation(rpe, buf);
                rpe->invalid = true;
                return false;
            }
        }
        return true;
    }

    bool PassOpen(WGPURenderPassEncoder rpe, const char *apiName)
    {
        if (!rpe)
            return false;
        if (rpe->ended)
        {
            // Pass is already ended → no future end()/finish() will surface a
            // deferred message. Fire directly so the test's pushErrorScope
            // wrapping the post-end call captures it.
            if (rpe->device)
            {
                std::string msg = std::string(apiName) + ": render pass encoder is already ended";
                rpe->device->reportError(WGPUErrorType_Validation,
                                         pwgpu::ToStringView(msg.c_str()));
            }
            return false;
        }
        if (rpe->parent && rpe->parent->finished)
        {
            if (rpe->device)
            {
                std::string msg = std::string(apiName) + ": parent command encoder is already finished";
                rpe->device->reportError(WGPUErrorType_Validation,
                                         pwgpu::ToStringView(msg.c_str()));
            }
            return false;
        }
        if (rpe->invalid)
            return false;
        return true;
    }

    void CollectBindGroupBarriers(WGPURenderPassEncoder rpe,
                                  std::vector<pe::ImageBarrierInfo> &imageBarriers,
                                  std::vector<pe::BufferBarrierInfo> &bufferBarriers);

    // Deferred beginRendering: Vulkan dynamic rendering forbids image layout
    // transitions inside the rendering scope. By deferring beginRendering until
    // the first draw-scope command, we merge attachment barriers and bind-group
    // barriers into a single vkCmdPipelineBarrier2 emitted just before
    // beginRendering — the lazy-barrier pattern used in PhasmaCore.
    void OpenRenderingIfNeeded(WGPURenderPassEncoder rpe)
    {
        if (!rpe || rpe->renderingActive)
            return;

        if (!rpe->bindGroupBarriersEmitted)
        {
            std::vector<pe::ImageBarrierInfo> imageBarriers =
                std::move(rpe->deferredAttachmentBarriers);
            std::vector<pe::BufferBarrierInfo> bufferBarriers;
            CollectBindGroupBarriers(rpe, imageBarriers, bufferBarriers);

            if (!bufferBarriers.empty())
                rpe->cmd->BufferBarriers(bufferBarriers);
            if (!imageBarriers.empty())
                rpe->cmd->ImageBarriers(imageBarriers);

            rpe->bindGroupBarriersEmitted = true;
        }

        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea = vk::Rect2D{{0, 0},
                                              {rpe->deferredRenderWidth, rpe->deferredRenderHeight}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount =
            static_cast<uint32_t>(rpe->deferredColorAttachments.size());
        renderingInfo.pColorAttachments = rpe->deferredColorAttachments.data();
        if (rpe->deferredHasDepth)
            renderingInfo.pDepthAttachment = &rpe->deferredDepthAtt;
        if (rpe->deferredHasStencil)
            renderingInfo.pStencilAttachment = &rpe->deferredStencilAtt;

        rpe->cmd->ApiHandle().beginRendering(renderingInfo);
        rpe->renderingActive = true;
    }

    bool RenderingActive(WGPURenderPassEncoder rpe, const char *apiName)
    {
        if (!PassOpen(rpe, apiName))
            return false;
        return true;
    }

    bool LayoutsEqual(const std::vector<WGPUTextureFormat> &a, WGPUTextureFormat aDsFormat, uint32_t aSc,
                      const std::vector<WGPUTextureFormat> &b, WGPUTextureFormat bDsFormat, uint32_t bSc)
    {
        if (aDsFormat != bDsFormat || aSc != bSc)
            return false;
        size_t aLen = a.size(), bLen = b.size();
        while (aLen > 0 && a[aLen - 1] == WGPUTextureFormat_Undefined)
            --aLen;
        while (bLen > 0 && b[bLen - 1] == WGPUTextureFormat_Undefined)
            --bLen;
        if (aLen != bLen)
            return false;
        for (size_t i = 0; i < aLen; i++)
        {
            if (a[i] != b[i])
                return false;
        }
        return true;
    }

    vk::AccessFlags2 BufferAccessForUsage(pwgpu::BufferUsageKind kind)
    {
        switch (kind)
        {
        case pwgpu::BufferUsageKind::Constant:
            return vk::AccessFlagBits2::eUniformRead;
        case pwgpu::BufferUsageKind::StorageRead:
            return vk::AccessFlagBits2::eShaderStorageRead;
        case pwgpu::BufferUsageKind::Storage:
            return vk::AccessFlagBits2::eShaderStorageRead |
                   vk::AccessFlagBits2::eShaderStorageWrite;
        default:
            return vk::AccessFlagBits2::eNone;
        }
    }

    vk::AccessFlags2 TextureAccessForUsage(pwgpu::SubresourceUsageKind kind)
    {
        switch (kind)
        {
        case pwgpu::SubresourceUsageKind::Sampled:
            return vk::AccessFlagBits2::eShaderSampledRead;
        case pwgpu::SubresourceUsageKind::ReadOnlyStorage:
            return vk::AccessFlagBits2::eShaderStorageRead;
        case pwgpu::SubresourceUsageKind::WriteOnlyStorage:
            return vk::AccessFlagBits2::eShaderStorageWrite;
        case pwgpu::SubresourceUsageKind::ReadWriteStorage:
            return vk::AccessFlagBits2::eShaderStorageRead |
                   vk::AccessFlagBits2::eShaderStorageWrite;
        default:
            return vk::AccessFlagBits2::eNone;
        }
    }

    vk::ImageLayout TextureLayoutForUsage(pwgpu::SubresourceUsageKind kind)
    {
        return kind == pwgpu::SubresourceUsageKind::Sampled
                   ? vk::ImageLayout::eShaderReadOnlyOptimal
                   : vk::ImageLayout::eGeneral;
    }

    void CollectBindGroupBarriers(WGPURenderPassEncoder rpe,
                                  std::vector<pe::ImageBarrierInfo> &imageBarriers,
                                  std::vector<pe::BufferBarrierInfo> &bufferBarriers)
    {
        if (!rpe || !rpe->pipeline || !rpe->pipeline->layout)
            return;

        auto &bgls = rpe->pipeline->layout->bindGroupLayouts;
        for (size_t i = 0; i < rpe->currentBindGroups.size() && i < bgls.size(); ++i)
        {
            if (!bgls[i])
                continue;

            WGPUBindGroupImpl *bg = rpe->currentBindGroups[i];
            if (!bg || bg->invalid)
                continue;

            for (const auto &use : bg->textureUses)
            {
                if (!use.view || !use.view->texture || !use.view->texture->image)
                    continue;

                pe::ImageBarrierInfo barrier{};
                barrier.image = use.view->texture->image;
                barrier.stageFlags = vk::PipelineStageFlagBits2::eAllGraphics;
                barrier.accessMask = TextureAccessForUsage(use.kind);
                barrier.layout = TextureLayoutForUsage(use.kind);
                barrier.baseMipLevel = use.view->baseMipLevel;
                barrier.mipLevels = use.view->mipLevelCount;
                barrier.baseArrayLayer = use.view->baseArrayLayer;
                barrier.arrayLayers = use.view->arrayLayerCount;
                imageBarriers.push_back(barrier);
            }

            for (const auto &use : bg->bufferUses)
            {
                if (!use.buffer || !use.buffer->peBuffer)
                    continue;

                pe::BufferBarrierInfo barrier{};
                barrier.buffer = use.buffer->peBuffer;
                barrier.stageMask = vk::PipelineStageFlagBits2::eAllGraphics;
                barrier.accessMask = BufferAccessForUsage(use.kind);
                bufferBarriers.push_back(barrier);
            }
        }
    }
} // namespace

extern "C"
{

    void wgpuRenderPassEncoderAddRef(WGPURenderPassEncoder rpe)
    {
        if (rpe)
            rpe->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuRenderPassEncoderRelease(WGPURenderPassEncoder rpe)
    {
        if (!rpe)
            return;
        if (rpe->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            for (auto *p : rpe->retainedPipelines)
                wgpuRenderPipelineRelease(p);
            for (auto *bg : rpe->retainedBindGroups)
                wgpuBindGroupRelease(bg);
            for (auto *rb : rpe->retainedBundles)
                wgpuRenderBundleRelease(rb);
            for (auto *v : rpe->retainedViews)
                wgpuTextureViewRelease(v);
            for (auto *sv : rpe->ownedSliceViews)
                pe::ImageView::Destroy(sv);
            if (rpe->timestampQuerySet)
                wgpuQuerySetRelease(rpe->timestampQuerySet);
            if (rpe->occlusionQuerySet)
                wgpuQuerySetRelease(rpe->occlusionQuerySet);
            delete rpe;
        }
    }

    void wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder rpe, WGPURenderPipeline pipeline)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderSetPipeline"))
            return;
        if (!pipeline || pipeline->invalid || pipeline->vkPipeline == VK_NULL_HANDLE)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetPipeline: pipeline is null or invalid");
            rpe->invalid = true;
            return;
        }

        if (pipeline->device != rpe->device)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetPipeline: pipeline was created by a "
                                 "different device than the render pass encoder");
            rpe->invalid = true;
            return;
        }
        if (!LayoutsEqual(rpe->colorFormats, rpe->depthStencilFormat, rpe->sampleCount,
                          pipeline->colorFormats, pipeline->depthStencilFormat, pipeline->sampleCount))
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetPipeline: pipeline render targets layout "
                                 "does not match render pass layout "
                                 "(colorFormats/depthStencilFormat/sampleCount mismatch)");
            rpe->invalid = true;
            return;
        }
        if (pipeline->writesDepth && rpe->depthReadOnly)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetPipeline: pipeline writesDepth is true "
                                 "but render pass depthReadOnly is true");
            rpe->invalid = true;
            return;
        }
        if (pipeline->writesStencil && rpe->stencilReadOnly)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetPipeline: pipeline writesStencil is true "
                                 "but render pass stencilReadOnly is true");
            rpe->invalid = true;
            return;
        }

        rpe->pipeline = pipeline;
        rpe->bindingStateInvalidated = false;

        wgpuRenderPipelineAddRef(pipeline);
        rpe->retainedPipelines.push_back(pipeline);

        rpe->cmd->ApiHandle().bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->vkPipeline);

        if (pipeline->layout)
        {
            auto &bgls = pipeline->layout->bindGroupLayouts;
            vk::PipelineLayout vkLayout(pipeline->layout->vkLayout);
            for (size_t i = 0; i < rpe->currentBindGroups.size() && i < bgls.size(); ++i)
            {
                auto *bg = rpe->currentBindGroups[i];
                if (!bg || !bg->descriptor || !bgls[i])
                    continue;
                if (!BglGroupEquivalent(bg->layout, bgls[i]))
                    continue;
                vk::DescriptorSet ds = bg->descriptor->ApiHandle();
                rpe->cmd->ApiHandle().bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics, vkLayout,
                    static_cast<uint32_t>(i), 1, &ds, 0, nullptr);
            }
        }
    }

    void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder rpe, uint32_t groupIndex,
                                           WGPUBindGroup group,
                                           size_t dynamicOffsetCount, uint32_t const *dynamicOffsets)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderSetBindGroup"))
            return;

        if (rpe->device && groupIndex >= rpe->device->limits.maxBindGroups)
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "wgpuRenderPassEncoderSetBindGroup: index (%u) must be < "
                          "maxBindGroups (%u)",
                          groupIndex, rpe->device->limits.maxBindGroups);
            ReportPassValidation(rpe, buf);
            rpe->invalid = true;
            return;
        }

        if (group)
        {
            if (group->device != rpe->device)
            {
                ReportPassValidation(rpe,
                                     "wgpuRenderPassEncoderSetBindGroup: bindGroup is not valid "
                                     "to use with this encoder (cross-device)");
                rpe->invalid = true;
                return;
            }
            if (group->invalid)
            {
                if (group->invalidFromDestroyedResource)
                {
                    // §3.3: defer to queue.submit, don't fire at finish.
                    rpe->deferredResourceError = true;
                }
                else
                {
                    ReportPassValidation(
                        rpe,
                        "wgpuRenderPassEncoderSetBindGroup: bindGroup is not valid "
                        "to use with this encoder (invalid bindGroup)");
                    rpe->invalid = true;
                }
                return;
            }
        }

        if (group)
        {
            for (auto &use : group->textureUses)
            {
                std::string err;
                if (!rpe->usageScope.AddView(use.view, use.kind, err))
                    rpe->usageScopeValid = false;
            }
            for (auto &use : group->bufferUses)
            {
                std::string err;
                if (!rpe->usageScope.AddBuffer(use.buffer, use.kind, err))
                    rpe->usageScopeValid = false;
            }
            wgpuBindGroupAddRef(group);
            rpe->retainedBindGroups.push_back(group);
        }

        if (rpe->device && groupIndex < rpe->device->limits.maxBindGroups)
        {
            if (rpe->currentBindGroups.size() <= groupIndex)
                rpe->currentBindGroups.resize(groupIndex + 1, nullptr);
            rpe->currentBindGroups[groupIndex] = group;
        }

        if (group && group->layout)
        {
            if (static_cast<uint32_t>(dynamicOffsetCount) != group->layout->dynamicOffsetCount)
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "wgpuRenderPassEncoderSetBindGroup: dynamicOffsetCount (%zu) does not match "
                              "bind group layout dynamicOffsetCount (%u)",
                              dynamicOffsetCount, group->layout->dynamicOffsetCount);
                ReportPassValidation(rpe, buf);
                rpe->invalid = true;
                return;
            }
            if (dynamicOffsetCount > 0 && !dynamicOffsets)
            {
                ReportPassValidation(rpe,
                                     "wgpuRenderPassEncoderSetBindGroup: dynamicOffsets is null but "
                                     "dynamicOffsetCount > 0");
                rpe->invalid = true;
                return;
            }
            if (rpe->device && dynamicOffsetCount > 0 && dynamicOffsets)
            {
                std::vector<const WGPUBindGroupLayoutEntryResolved *> dynLayoutEntries;
                for (auto &e : group->layout->entries)
                    if (e.buffer.hasDynamicOffset)
                        dynLayoutEntries.push_back(&e);
                std::sort(dynLayoutEntries.begin(), dynLayoutEntries.end(),
                          [](auto *a, auto *b)
                          { return a->binding < b->binding; });
                for (uint32_t i = 0; i < dynamicOffsetCount && i < dynLayoutEntries.size() &&
                                     i < group->dynamicBindings.size();
                     ++i)
                {
                    uint32_t offset = dynamicOffsets[i];
                    auto &dyn = group->dynamicBindings[i];
                    bool isUniform = dynLayoutEntries[i]->buffer.type == WGPUBufferBindingType_Uniform;
                    uint32_t align = isUniform ? rpe->device->limits.minUniformBufferOffsetAlignment
                                               : rpe->device->limits.minStorageBufferOffsetAlignment;
                    if (align > 0 && offset % align != 0)
                    {
                        char buf[192];
                        std::snprintf(buf, sizeof(buf),
                                      "wgpuRenderPassEncoderSetBindGroup: dynamicOffsets[%u]=%u is not a "
                                      "multiple of %s (%u) for binding %u",
                                      i, offset,
                                      isUniform ? "minUniformBufferOffsetAlignment"
                                                : "minStorageBufferOffsetAlignment",
                                      align, dynLayoutEntries[i]->binding);
                        ReportPassValidation(rpe, buf);
                        rpe->invalid = true;
                        return;
                    }
                    if (dyn.buffer)
                    {
                        uint64_t bufSize = dyn.buffer->size;
                        uint64_t effOffset = dyn.baseOffset + static_cast<uint64_t>(offset);
                        if (effOffset > bufSize || dyn.bindingSize > bufSize - effOffset)
                        {
                            char buf[224];
                            std::snprintf(buf, sizeof(buf),
                                          "wgpuRenderPassEncoderSetBindGroup: dynamicOffsets[%u]=%u "
                                          "exceeds buffer bounds for binding %u "
                                          "(baseOffset=%llu, bindingSize=%llu, bufferSize=%llu)",
                                          i, offset, dyn.binding,
                                          (unsigned long long)dyn.baseOffset,
                                          (unsigned long long)dyn.bindingSize,
                                          (unsigned long long)bufSize);
                            ReportPassValidation(rpe, buf);
                            rpe->invalid = true;
                            return;
                        }
                    }
                }
            }
        }

        if (!rpe->pipeline || !rpe->pipeline->layout)
            return;

        if (!group || !group->descriptor)
            return;

        auto &bgls = rpe->pipeline->layout->bindGroupLayouts;
        if (groupIndex >= bgls.size() || !bgls[groupIndex])
            return;
        if (!BglGroupEquivalent(group->layout, bgls[groupIndex]))
            return;

        vk::PipelineLayout vkLayout(rpe->pipeline->layout->vkLayout);
        vk::DescriptorSet ds = group->descriptor->ApiHandle();

        rpe->cmd->ApiHandle().bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            vkLayout, groupIndex,
            1, &ds,
            static_cast<uint32_t>(dynamicOffsetCount), dynamicOffsets);
    }

    void wgpuRenderPassEncoderSetVertexBuffer(WGPURenderPassEncoder rpe, uint32_t slot,
                                              WGPUBuffer buffer, uint64_t offset, uint64_t size)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetVertexBuffer"))
            return;

        if (rpe->device && slot >= rpe->device->limits.maxVertexBuffers)
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "wgpuRenderPassEncoderSetVertexBuffer: slot (%u) must be < "
                          "maxVertexBuffers (%u)",
                          slot, rpe->device->limits.maxVertexBuffers);
            ReportPassValidation(rpe, buf);
            rpe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetVertexBuffer: offset must be a "
                                 "multiple of 4");
            rpe->invalid = true;
            return;
        }
        uint64_t bufferSize = buffer ? buffer->size : 0u;
        if (size == WGPU_WHOLE_SIZE)
            size = (offset > bufferSize) ? 0u : (bufferSize - offset);
        if (offset > bufferSize || size > bufferSize - offset)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetVertexBuffer: offset + size "
                                 "exceeds buffer size");
            rpe->invalid = true;
            return;
        }

        if (!buffer)
        {
            if (slot < rpe->boundVertexBuffers.size())
                rpe->boundVertexBuffers[slot] = {};
            return;
        }

        if (buffer->device != rpe->device || buffer->invalid)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetVertexBuffer: buffer is not valid "
                                 "to use with this encoder (cross-device or invalid buffer)");
            rpe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Vertex))
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetVertexBuffer: buffer usage must "
                                 "contain VERTEX");
            rpe->invalid = true;
            return;
        }

        if (rpe->boundVertexBuffers.size() <= slot)
            rpe->boundVertexBuffers.resize(slot + 1);
        rpe->boundVertexBuffers[slot].bound = true;
        rpe->boundVertexBuffers[slot].size = size;

        {
            std::string err;
            if (!rpe->usageScope.AddBuffer(buffer, pwgpu::BufferUsageKind::Input, err))
                rpe->usageScopeValid = false;
        }

        // Destroyed buffer defers to queue.submit() per spec.
        if (buffer->internalState == BufferInternalState::Destroyed || !buffer->peBuffer)
        {
            rpe->usedBuffers.push_back(buffer);
            return;
        }

        vk::Buffer vkBuf = buffer->peBuffer->ApiHandle();
        vk::DeviceSize vkOffset = static_cast<vk::DeviceSize>(offset);
        rpe->cmd->ApiHandle().bindVertexBuffers(slot, 1, &vkBuf, &vkOffset);
        rpe->usedBuffers.push_back(buffer);
    }

    void wgpuRenderPassEncoderSetIndexBuffer(WGPURenderPassEncoder rpe, WGPUBuffer buffer,
                                             WGPUIndexFormat format, uint64_t offset, uint64_t size)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetIndexBuffer"))
            return;

        if (!buffer)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetIndexBuffer: buffer is null");
            rpe->invalid = true;
            return;
        }
        if (buffer->device != rpe->device || buffer->invalid)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetIndexBuffer: buffer is not valid "
                                 "to use with this encoder (cross-device or invalid buffer)");
            rpe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Index))
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetIndexBuffer: buffer usage must "
                                 "contain INDEX");
            rpe->invalid = true;
            return;
        }
        if (format != WGPUIndexFormat_Uint16 && format != WGPUIndexFormat_Uint32)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetIndexBuffer: indexFormat must be "
                                 "Uint16 or Uint32");
            rpe->invalid = true;
            return;
        }
        uint32_t indexSize = (format == WGPUIndexFormat_Uint16) ? 2u : 4u;
        if (offset % indexSize != 0u)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetIndexBuffer: offset must be a "
                                 "multiple of indexFormat byte size");
            rpe->invalid = true;
            return;
        }
        uint64_t boundSize = size;
        if (boundSize == WGPU_WHOLE_SIZE)
            boundSize = (offset > buffer->size) ? 0u : (buffer->size - offset);
        if (offset > buffer->size || boundSize > buffer->size - offset)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderSetIndexBuffer: offset + size "
                                 "exceeds buffer size");
            rpe->invalid = true;
            return;
        }

        rpe->indexBuffer = buffer;
        rpe->indexFormat = format;
        rpe->indexBufferSize = boundSize;

        {
            std::string err;
            if (!rpe->usageScope.AddBuffer(buffer, pwgpu::BufferUsageKind::Input, err))
                rpe->usageScopeValid = false;
        }

        if (buffer->internalState == BufferInternalState::Destroyed || !buffer->peBuffer)
        {
            rpe->usedBuffers.push_back(buffer);
            return;
        }

        vk::IndexType indexType = (format == WGPUIndexFormat_Uint16) ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
        rpe->cmd->ApiHandle().bindIndexBuffer(buffer->peBuffer->ApiHandle(), offset, indexType);
        rpe->usedBuffers.push_back(buffer);
    }

    void wgpuRenderPassEncoderDraw(WGPURenderPassEncoder rpe, uint32_t vertexCount,
                                   uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDraw"))
            return;
        if (!rpe->pipeline || rpe->bindingStateInvalidated)
            return;
        if (!ValidateBindGroupCompat(rpe))
            return;
        if (!ValidateDrawVertexState(rpe->pipeline->vertexBufferLayouts, rpe->boundVertexBuffers,
                                     firstVertex, vertexCount, firstInstance, instanceCount, false))
        {
            rpe->invalid = true;
            return;
        }
        rpe->drawCount++;
        OpenRenderingIfNeeded(rpe);
        rpe->cmd->ApiHandle().draw(vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void wgpuRenderPassEncoderDrawIndexed(WGPURenderPassEncoder rpe, uint32_t indexCount,
                                          uint32_t instanceCount, uint32_t firstIndex,
                                          int32_t baseVertex, uint32_t firstInstance)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDrawIndexed"))
            return;
        if (!rpe->indexBuffer || rpe->indexFormat == WGPUIndexFormat_Undefined)
        {
            rpe->invalid = true;
            return;
        }
        uint32_t indexSize = (rpe->indexFormat == WGPUIndexFormat_Uint16) ? 2u : 4u;
        uint64_t maxIndices = rpe->indexBufferSize / indexSize;
        uint64_t requiredEnd = static_cast<uint64_t>(firstIndex) + static_cast<uint64_t>(indexCount);
        if (requiredEnd > maxIndices)
        {
            rpe->invalid = true;
            return;
        }
        if (!rpe->pipeline || rpe->bindingStateInvalidated)
            return;
        if (!ValidateBindGroupCompat(rpe))
            return;
        if (!ValidateDrawVertexState(rpe->pipeline->vertexBufferLayouts, rpe->boundVertexBuffers,
                                     0, 0, firstInstance, instanceCount, true))
        {
            rpe->invalid = true;
            return;
        }
        rpe->drawCount++;
        OpenRenderingIfNeeded(rpe);
        rpe->cmd->ApiHandle().drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
    }

    void wgpuRenderPassEncoderDrawIndirect(WGPURenderPassEncoder rpe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDrawIndirect"))
            return;

        if (!buffer)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderDrawIndirect: indirectBuffer is null");
            rpe->invalid = true;
            return;
        }
        if (buffer->device != rpe->device)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderDrawIndirect: indirectBuffer was created by a "
                                 "different device than the render pass encoder");
            rpe->invalid = true;
            return;
        }
        if (buffer->invalid)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderDrawIndirect: indirectBuffer is invalid");
            rpe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderDrawIndirect: indirectBuffer.usage does not "
                                 "contain INDIRECT");
            rpe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "wgpuRenderPassEncoderDrawIndirect: indirectOffset (%llu) is not a multiple of 4",
                          (unsigned long long)offset);
            ReportPassValidation(rpe, buf);
            rpe->invalid = true;
            return;
        }
        constexpr uint64_t kDrawArgsSize = sizeof(VkDrawIndirectCommand);
        if (offset > buffer->size || kDrawArgsSize > buffer->size - offset)
        {
            char buf[224];
            std::snprintf(buf, sizeof(buf),
                          "wgpuRenderPassEncoderDrawIndirect: indirectOffset (%llu) + "
                          "sizeof(indirect draw parameters) (%llu) exceeds indirectBuffer.size (%llu)",
                          (unsigned long long)offset, (unsigned long long)kDrawArgsSize,
                          (unsigned long long)buffer->size);
            ReportPassValidation(rpe, buf);
            rpe->invalid = true;
            return;
        }

        {
            std::string err;
            if (!rpe->usageScope.AddBuffer(buffer, pwgpu::BufferUsageKind::Input, err))
                rpe->usageScopeValid = false;
        }

        if (!rpe->pipeline || rpe->bindingStateInvalidated)
            return;
        if (!ValidateBindGroupCompat(rpe))
            return;
        if (!ValidateDrawBindPresence(rpe->pipeline->vertexBufferLayouts, rpe->boundVertexBuffers))
        {
            rpe->invalid = true;
            return;
        }
        if (buffer->internalState == BufferInternalState::Destroyed || !buffer->peBuffer)
        {
            rpe->usedBuffers.push_back(buffer);
            return;
        }

        rpe->drawCount++;
        OpenRenderingIfNeeded(rpe);
        rpe->cmd->ApiHandle().drawIndirect(buffer->peBuffer->ApiHandle(), offset, 1, sizeof(VkDrawIndirectCommand));
        rpe->usedBuffers.push_back(buffer);
    }

    void wgpuRenderPassEncoderDrawIndexedIndirect(WGPURenderPassEncoder rpe, WGPUBuffer buffer, uint64_t offset)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderDrawIndexedIndirect"))
            return;

        if (!buffer)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderDrawIndexedIndirect: indirectBuffer is null");
            rpe->invalid = true;
            return;
        }
        if (buffer->device != rpe->device)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderDrawIndexedIndirect: indirectBuffer was created "
                                 "by a different device than the render pass encoder");
            rpe->invalid = true;
            return;
        }
        if (buffer->invalid)
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderDrawIndexedIndirect: indirectBuffer is invalid");
            rpe->invalid = true;
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_Indirect))
        {
            ReportPassValidation(rpe,
                                 "wgpuRenderPassEncoderDrawIndexedIndirect: indirectBuffer.usage does "
                                 "not contain INDIRECT");
            rpe->invalid = true;
            return;
        }
        if (offset % 4u != 0u)
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "wgpuRenderPassEncoderDrawIndexedIndirect: indirectOffset (%llu) is not a "
                          "multiple of 4",
                          (unsigned long long)offset);
            ReportPassValidation(rpe, buf);
            rpe->invalid = true;
            return;
        }
        constexpr uint64_t kDrawArgsSize = sizeof(VkDrawIndexedIndirectCommand);
        if (offset > buffer->size || kDrawArgsSize > buffer->size - offset)
        {
            char buf[224];
            std::snprintf(buf, sizeof(buf),
                          "wgpuRenderPassEncoderDrawIndexedIndirect: indirectOffset (%llu) + "
                          "sizeof(indirect drawIndexed parameters) (%llu) exceeds indirectBuffer.size "
                          "(%llu)",
                          (unsigned long long)offset, (unsigned long long)kDrawArgsSize,
                          (unsigned long long)buffer->size);
            ReportPassValidation(rpe, buf);
            rpe->invalid = true;
            return;
        }

        {
            std::string err;
            if (!rpe->usageScope.AddBuffer(buffer, pwgpu::BufferUsageKind::Input, err))
                rpe->usageScopeValid = false;
        }

        if (!rpe->pipeline || rpe->bindingStateInvalidated)
            return;
        if (!ValidateBindGroupCompat(rpe))
            return;
        if (!ValidateDrawBindPresence(rpe->pipeline->vertexBufferLayouts, rpe->boundVertexBuffers))
        {
            rpe->invalid = true;
            return;
        }
        if (buffer->internalState == BufferInternalState::Destroyed || !buffer->peBuffer)
        {
            rpe->usedBuffers.push_back(buffer);
            return;
        }

        rpe->drawCount++;
        OpenRenderingIfNeeded(rpe);
        rpe->cmd->ApiHandle().drawIndexedIndirect(buffer->peBuffer->ApiHandle(), offset, 1, sizeof(VkDrawIndexedIndirectCommand));
        rpe->usedBuffers.push_back(buffer);
    }

    void wgpuRenderPassEncoderSetViewport(WGPURenderPassEncoder rpe,
                                          float x, float y, float width, float height,
                                          float minDepth, float maxDepth)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetViewport"))
            return;

        // §17.2.2: maxViewportRange = maxTextureDimension2D * 2.
        const float maxDim = rpe->device ? static_cast<float>(rpe->device->limits.maxTextureDimension2D) : 0.0f;
        const float maxRange = maxDim * 2.0f;
        if (!(width >= 0.0f && width <= maxDim) || !(height >= 0.0f && height <= maxDim) ||
            !(x >= -maxRange) || !(y >= -maxRange) ||
            !(x + width <= maxRange - 1.0f) || !(y + height <= maxRange - 1.0f) ||
            !(minDepth >= 0.0f && minDepth <= 1.0f) ||
            !(maxDepth >= 0.0f && maxDepth <= 1.0f) ||
            !(minDepth <= maxDepth))
        {
            rpe->invalid = true;
            return;
        }

        vk::Viewport vp{x, y, width, height, minDepth, maxDepth};
        rpe->cmd->ApiHandle().setViewport(0, 1, &vp);
    }

    void wgpuRenderPassEncoderSetScissorRect(WGPURenderPassEncoder rpe,
                                             uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetScissorRect"))
            return;

        if (x > rpe->attachmentWidth || width > rpe->attachmentWidth - x ||
            y > rpe->attachmentHeight || height > rpe->attachmentHeight - y)
        {
            rpe->invalid = true;
            return;
        }

        vk::Rect2D scissor{{static_cast<int32_t>(x), static_cast<int32_t>(y)}, {width, height}};
        rpe->cmd->ApiHandle().setScissor(0, 1, &scissor);
    }

    void wgpuRenderPassEncoderSetBlendConstant(WGPURenderPassEncoder rpe, WGPUColor const *color)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetBlendConstant"))
            return;
        if (!color)
            return;

        float constants[4] = {static_cast<float>(color->r), static_cast<float>(color->g),
                              static_cast<float>(color->b), static_cast<float>(color->a)};
        rpe->cmd->ApiHandle().setBlendConstants(constants);
    }

    void wgpuRenderPassEncoderSetStencilReference(WGPURenderPassEncoder rpe, uint32_t reference)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetStencilReference"))
            return;

        rpe->cmd->ApiHandle().setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, reference);
    }

    void wgpuRenderPassEncoderBeginOcclusionQuery(WGPURenderPassEncoder rpe, uint32_t queryIndex)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderBeginOcclusionQuery"))
            return;

        if (!rpe->occlusionQuerySet)
        {
            rpe->invalid = true;
            return;
        }
        if (rpe->occlusionQueryActive)
        {
            rpe->invalid = true;
            return;
        }
        if (queryIndex >= rpe->occlusionQuerySet->count)
        {
            rpe->invalid = true;
            return;
        }
        if (rpe->usedOcclusionIndices.count(queryIndex) != 0)
        {
            rpe->invalid = true;
            return;
        }

        const bool isFirstQueryThisPass = rpe->usedOcclusionIndices.empty();

        rpe->occlusionQueryActive = true;
        rpe->activeOcclusionIndex = queryIndex;
        rpe->usedOcclusionIndices.insert(queryIndex);
        rpe->occlusionQuerySet->beganIndices.insert(queryIndex);

        // Pool is host-reset at createQuerySet, but slots written by prior submits
        // are left "available" and re-begin violates VUID-vkCmdBeginQuery-None-00807.
        // Reset the full pool range once per pass on the first beginQuery, before
        // vkCmdBeginRendering opens (vkCmdResetQueryPool is forbidden inside a pass).
        // A pass that binds occlusionQuerySet but never calls beginQuery emits no
        // reset, preserving prior-submission slot data (multi_resolve CTS semantics).
        if (isFirstQueryThisPass && !rpe->renderingActive &&
            rpe->occlusionQuerySet->queryPool && rpe->occlusionQuerySet->count > 0)
        {
            rpe->cmd->ApiHandle().resetQueryPool(
                rpe->occlusionQuerySet->queryPool, 0, rpe->occlusionQuerySet->count);
        }

        OpenRenderingIfNeeded(rpe);
        if (rpe->occlusionQuerySet->queryPool)
            rpe->cmd->ApiHandle().beginQuery(
                rpe->occlusionQuerySet->queryPool, queryIndex, vk::QueryControlFlags{});
    }

    void wgpuRenderPassEncoderEndOcclusionQuery(WGPURenderPassEncoder rpe)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderEndOcclusionQuery"))
            return;

        if (!rpe->occlusionQueryActive)
        {
            rpe->invalid = true;
            return;
        }

        rpe->occlusionQueryActive = false;

        uint32_t lastIndex = rpe->activeOcclusionIndex;
        rpe->activeOcclusionIndex = UINT32_MAX;
        if (rpe->occlusionQuerySet && rpe->occlusionQuerySet->queryPool)
            rpe->cmd->ApiHandle().endQuery(
                rpe->occlusionQuerySet->queryPool, lastIndex);
    }

    void wgpuRenderPassEncoderExecuteBundles(WGPURenderPassEncoder rpe,
                                             size_t bundleCount, WGPURenderBundle const *bundles)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderExecuteBundles"))
            return;

        for (size_t i = 0; i < bundleCount; i++)
        {
            auto *bundle = bundles[i];
            if (!bundle || bundle->invalid)
            {
                if (bundle && !bundle->deferredErrorMessage.empty() &&
                    rpe->deferredErrorMessage.empty())
                    rpe->deferredErrorMessage = bundle->deferredErrorMessage;
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                              "wgpuRenderPassEncoderExecuteBundles: bundle[%zu] is invalid", i);
                ReportPassValidation(rpe, buf);
                rpe->invalid = true;
                return;
            }
            if (bundle->device != rpe->device)
            {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "wgpuRenderPassEncoderExecuteBundles: bundle[%zu] was created by a "
                              "different device than the render pass encoder",
                              i);
                ReportPassValidation(rpe, buf);
                rpe->invalid = true;
                return;
            }

            if (!LayoutsEqual(rpe->colorFormats, rpe->depthStencilFormat, rpe->sampleCount,
                              bundle->colorFormats, bundle->depthStencilFormat, bundle->sampleCount))
            {
                char buf[224];
                std::snprintf(buf, sizeof(buf),
                              "wgpuRenderPassEncoderExecuteBundles: bundle[%zu] layout does not match "
                              "render pass layout (colorFormats/depthStencilFormat/sampleCount mismatch)",
                              i);
                ReportPassValidation(rpe, buf);
                rpe->invalid = true;
                return;
            }

            if (rpe->depthReadOnly && !bundle->depthReadOnly)
            {
                char buf[224];
                std::snprintf(buf, sizeof(buf),
                              "wgpuRenderPassEncoderExecuteBundles: bundle[%zu] depthReadOnly is false "
                              "but render pass depthReadOnly is true",
                              i);
                ReportPassValidation(rpe, buf);
                rpe->invalid = true;
                return;
            }

            if (rpe->stencilReadOnly && !bundle->stencilReadOnly)
            {
                char buf[224];
                std::snprintf(buf, sizeof(buf),
                              "wgpuRenderPassEncoderExecuteBundles: bundle[%zu] stencilReadOnly is false "
                              "but render pass stencilReadOnly is true",
                              i);
                ReportPassValidation(rpe, buf);
                rpe->invalid = true;
                return;
            }
        }

        if (!rpe->renderingActive)
        {
            std::vector<pe::BufferBarrierInfo> bundleBufBarriers;
            for (size_t i = 0; i < bundleCount; i++)
            {
                auto *bundle = bundles[i];
                if (!bundle)
                    continue;
                for (const auto &entry : bundle->usageScope.bufferMap)
                {
                    WGPUBufferImpl *buf = entry.first;
                    if (!buf || !buf->peBuffer)
                        continue;
                    const uint8_t mask = entry.second;
                    pe::BufferBarrierInfo barrier{};
                    barrier.buffer = buf->peBuffer;
                    barrier.stageMask = vk::PipelineStageFlags2{};
                    barrier.accessMask = vk::AccessFlags2{};
                    // Storage / uniform reads in the vertex/fragment stages.
                    if (mask & static_cast<uint8_t>(pwgpu::BufferUsageKind::Storage))
                    {
                        barrier.stageMask |= vk::PipelineStageFlagBits2::eAllGraphics;
                        barrier.accessMask |= vk::AccessFlagBits2::eShaderStorageRead |
                                              vk::AccessFlagBits2::eShaderStorageWrite;
                    }
                    if (mask & static_cast<uint8_t>(pwgpu::BufferUsageKind::StorageRead))
                    {
                        barrier.stageMask |= vk::PipelineStageFlagBits2::eAllGraphics;
                        barrier.accessMask |= vk::AccessFlagBits2::eShaderStorageRead;
                    }
                    if (mask & static_cast<uint8_t>(pwgpu::BufferUsageKind::Constant))
                    {
                        barrier.stageMask |= vk::PipelineStageFlagBits2::eAllGraphics;
                        barrier.accessMask |= vk::AccessFlagBits2::eUniformRead;
                    }
                    // Vertex/index/indirect inputs have distinct stages
                    // that must match their access bits (VUID-03900/01/other).
                    if (mask & static_cast<uint8_t>(pwgpu::BufferUsageKind::Input))
                    {
                        if (buf->usage & WGPUBufferUsage_Indirect)
                        {
                            barrier.stageMask |= vk::PipelineStageFlagBits2::eDrawIndirect;
                            barrier.accessMask |= vk::AccessFlagBits2::eIndirectCommandRead;
                        }
                        if (buf->usage & WGPUBufferUsage_Index)
                        {
                            barrier.stageMask |= vk::PipelineStageFlagBits2::eIndexInput;
                            barrier.accessMask |= vk::AccessFlagBits2::eIndexRead;
                        }
                        if (buf->usage & WGPUBufferUsage_Vertex)
                        {
                            barrier.stageMask |= vk::PipelineStageFlagBits2::eVertexAttributeInput;
                            barrier.accessMask |= vk::AccessFlagBits2::eVertexAttributeRead;
                        }
                    }
                    if (!barrier.stageMask || !barrier.accessMask)
                        continue;
                    bundleBufBarriers.push_back(barrier);
                }
            }
            if (!bundleBufBarriers.empty())
                rpe->cmd->BufferBarriers(bundleBufBarriers);
        }

        OpenRenderingIfNeeded(rpe);
        for (size_t i = 0; i < bundleCount; i++)
        {
            auto *bundle = bundles[i];

            wgpuRenderBundleAddRef(bundle);
            rpe->retainedBundles.push_back(bundle);

            if (!bundle->usageScopeValid)
                rpe->usageScopeValid = false;
            if (bundle->deferredResourceError)
                rpe->deferredResourceError = true;
            std::string err;
            if (!rpe->usageScope.MergeFrom(bundle->usageScope, err))
                rpe->usageScopeValid = false;

            rpe->drawCount += bundle->drawCount;

            vk::CommandBuffer vkCmd = rpe->cmd->ApiHandle();
            for (auto &command : bundle->commands)
                command(vkCmd);
        }

        rpe->pipeline = nullptr;
        rpe->bindingStateInvalidated = true;
    }

    void wgpuRenderPassEncoderInsertDebugMarker(WGPURenderPassEncoder rpe, WGPUStringView label)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderInsertDebugMarker"))
            return;
        rpe->cmd->InsertDebugLabel(pwgpu::ToString(label));
    }

    void wgpuRenderPassEncoderPushDebugGroup(WGPURenderPassEncoder rpe, WGPUStringView groupLabel)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderPushDebugGroup"))
            return;
        rpe->debugGroupDepth++;
        rpe->cmd->BeginDebugRegion(pwgpu::ToString(groupLabel));
    }

    void wgpuRenderPassEncoderPopDebugGroup(WGPURenderPassEncoder rpe)
    {
        if (!PassOpen(rpe, "wgpuRenderPassEncoderPopDebugGroup"))
            return;
        if (rpe->debugGroupDepth == 0)
        {
            rpe->invalid = true;
            return;
        }
        rpe->debugGroupDepth--;
        rpe->cmd->EndDebugRegion();
    }

    void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder rpe)
    {
        if (!rpe)
            return;
        if (rpe->ended)
        {
            // No future fire path: pass is already ended.
            if (rpe->device)
                rpe->device->reportError(
                    WGPUErrorType_Validation,
                    pwgpu::ToStringView("RenderPassEncoder.end(): pass is already ended"));
            return;
        }
        if (rpe->parent && rpe->parent->finished)
        {
            // Parent encoder already finished — its finish() can't surface this.
            if (rpe->device)
                rpe->device->reportError(
                    WGPUErrorType_Validation,
                    pwgpu::ToStringView("RenderPassEncoder.end(): parent command encoder is already finished"));
            rpe->ended = true;
            return;
        }
        if (!rpe->wasOpened)
        {
            ReportPassValidation(rpe, "RenderPassEncoder.end(): pass was never opened (invalid begin)");
            if (rpe->parent)
                rpe->parent->invalid = true;
            rpe->ended = true;
            return;
        }

        if (rpe->drawCount > rpe->maxDrawCount)
            rpe->invalid = true;

        {
            const bool passInvalidForEncoder =
                (rpe->invalid && !rpe->deferredResourceError) ||
                !rpe->usageScopeValid || rpe->debugGroupDepth != 0 || rpe->occlusionQueryActive;
            if (passInvalidForEncoder && rpe->parent)
            {
                rpe->parent->invalid = true;
                if (rpe->parent->deferredErrorMessage.empty() && !rpe->deferredErrorMessage.empty())
                    rpe->parent->deferredErrorMessage = rpe->deferredErrorMessage;
            }
            if (rpe->deferredResourceError && rpe->parent)
                rpe->parent->deferredResourceError = true;
        }

        if (rpe->debugGroupDepth != 0)
            PE_WARN("[WebGPU] wgpuRenderPassEncoderEnd: %u debug group(s) still open", rpe->debugGroupDepth);

        if (rpe->occlusionQueryActive)
        {
            PE_WARN("[WebGPU] wgpuRenderPassEncoderEnd: auto-closing active occlusion query");
            uint32_t lastIndex = rpe->activeOcclusionIndex;
            rpe->cmd->ApiHandle().endQuery(rpe->occlusionQuerySet->queryPool, lastIndex);
            rpe->occlusionQueryActive = false;
            rpe->activeOcclusionIndex = UINT32_MAX;
        }

        // Empty/valid passes still need begin/endRendering so loadOp/storeOp fire.
        // Skip if the pass is invalid-for-encoder to avoid emitting work for
        // never-meant-to-execute passes.
        const bool passInvalidForEncoder =
            (rpe->invalid && !rpe->deferredResourceError) ||
            !rpe->usageScopeValid;
        if (!rpe->renderingActive && !passInvalidForEncoder)
            OpenRenderingIfNeeded(rpe);
        if (rpe->renderingActive)
        {
            rpe->cmd->ApiHandle().endRendering();
            rpe->renderingActive = false;
        }

        if (rpe->timestampQuerySet && rpe->endTimestampIndex != UINT32_MAX &&
            rpe->endTimestampIndex < rpe->timestampQuerySet->count)
        {
            rpe->cmd->ApiHandle().writeTimestamp2(
                vk::PipelineStageFlagBits2::eAllCommands,
                rpe->timestampQuerySet->queryPool, rpe->endTimestampIndex);
        }

        if (rpe->parent)
        {
            rpe->parent->retained.renderPipelines.insert(
                rpe->parent->retained.renderPipelines.end(),
                rpe->retainedPipelines.begin(), rpe->retainedPipelines.end());
            rpe->retainedPipelines.clear();

            rpe->parent->retained.bindGroups.insert(
                rpe->parent->retained.bindGroups.end(),
                rpe->retainedBindGroups.begin(), rpe->retainedBindGroups.end());
            rpe->retainedBindGroups.clear();

            rpe->parent->retained.renderBundles.insert(
                rpe->parent->retained.renderBundles.end(),
                rpe->retainedBundles.begin(), rpe->retainedBundles.end());
            rpe->retainedBundles.clear();

            rpe->parent->retained.usedBuffers.insert(
                rpe->parent->retained.usedBuffers.end(),
                rpe->usedBuffers.begin(), rpe->usedBuffers.end());
            rpe->usedBuffers.clear();

            rpe->parent->retained.textureViews.insert(
                rpe->parent->retained.textureViews.end(),
                rpe->retainedViews.begin(), rpe->retainedViews.end());
            rpe->retainedViews.clear();

            if (rpe->timestampQuerySet)
            {
                rpe->parent->retained.querySets.push_back(rpe->timestampQuerySet);
                rpe->timestampQuerySet = nullptr;
            }

            if (rpe->occlusionQuerySet)
            {
                rpe->parent->retained.querySets.push_back(rpe->occlusionQuerySet);
                rpe->occlusionQuerySet = nullptr;
            }

            rpe->parent->hasOpenPass = false;
        }

        rpe->ended = true;
    }

    void wgpuRenderPassEncoderSetLabel(WGPURenderPassEncoder rpe, WGPUStringView label)
    {
        if (rpe)
            rpe->label = pwgpu::ToString(label);
    }

} // extern "C"
