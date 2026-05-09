#include "RenderPass.h"
#include "CommandEncoder.h"
#include "RenderPipeline.h"
#include "PipelineBinding.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "Texture.h"
#include "RenderBundle.h"
#include "QuerySet.h"
#include "Device.h"
#include "FormatInfo.h"
#include "Utils.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanCommandBufferImpl.h"

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
    constexpr uint32_t kStencilReferenceMask = 0xffu;

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

    vk::AttachmentLoadOp ToVkLoadOp(PeLoadOp op)
    {
        switch (op)
        {
        case PE_LOAD_OP_CLEAR:
            return vk::AttachmentLoadOp::eClear;
        case PE_LOAD_OP_LOAD:
            return vk::AttachmentLoadOp::eLoad;
        case PE_LOAD_OP_DONT_CARE:
        default:
            return vk::AttachmentLoadOp::eDontCare;
        }
    }

    vk::AttachmentStoreOp ToVkStoreOp(WGPURenderPassAttachmentStoreOp op)
    {
        switch (op)
        {
        case WGPURenderPassAttachmentStoreOp::Store:
            return vk::AttachmentStoreOp::eStore;
        case WGPURenderPassAttachmentStoreOp::None:
            return vk::AttachmentStoreOp::eNone;
        case WGPURenderPassAttachmentStoreOp::DontCare:
        default:
            return vk::AttachmentStoreOp::eDontCare;
        }
    }

    void SetColorClearValueForFormat(vk::ClearColorValue &dst, WGPUTextureFormat format,
                                     const WGPUColor &src)
    {
        switch (pwgpu::GetColorFormatSampleType(format))
        {
        case pwgpu::ColorSampleType::Uint:
            dst.uint32[0] = static_cast<uint32_t>(src.r);
            dst.uint32[1] = static_cast<uint32_t>(src.g);
            dst.uint32[2] = static_cast<uint32_t>(src.b);
            dst.uint32[3] = static_cast<uint32_t>(src.a);
            break;
        case pwgpu::ColorSampleType::Sint:
            dst.int32[0] = static_cast<int32_t>(src.r);
            dst.int32[1] = static_cast<int32_t>(src.g);
            dst.int32[2] = static_cast<int32_t>(src.b);
            dst.int32[3] = static_cast<int32_t>(src.a);
            break;
        default:
            dst.float32[0] = static_cast<float>(src.r);
            dst.float32[1] = static_cast<float>(src.g);
            dst.float32[2] = static_cast<float>(src.b);
            dst.float32[3] = static_cast<float>(src.a);
            break;
        }
    }

    vk::RenderingAttachmentInfo ToVkRenderingAttachment(const WGPURenderPassAttachmentInfo &src)
    {
        vk::RenderingAttachmentInfo dst{};
        dst.imageView = vk::ImageView{PeFromBackendHandle<VkImageView>(src.imageView)};
        dst.imageLayout = pe::ToVkImageLayout(src.imageLayout);
        dst.loadOp = ToVkLoadOp(src.loadOp);
        dst.storeOp = ToVkStoreOp(src.storeOp);
        if (src.resolveImageView != 0)
        {
            dst.resolveMode = src.resolveAverage ? vk::ResolveModeFlagBits::eAverage
                                                 : vk::ResolveModeFlagBits::eSampleZero;
            dst.resolveImageView = vk::ImageView{PeFromBackendHandle<VkImageView>(src.resolveImageView)};
            dst.resolveImageLayout = pe::ToVkImageLayout(src.resolveImageLayout);
        }
        if (src.hasClearColor)
            SetColorClearValueForFormat(dst.clearValue.color, src.clearColorFormat, src.clearColor);
        if (src.hasClearDepth)
            dst.clearValue.depthStencil.depth = src.clearDepth;
        if (src.hasClearStencil)
            dst.clearValue.depthStencil.stencil = src.clearStencil;
        return dst;
    }

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

        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        colorAttachments.reserve(rpe->deferredColorAttachments.size());
        for (const auto &attachment : rpe->deferredColorAttachments)
            colorAttachments.push_back(ToVkRenderingAttachment(attachment));

        vk::RenderingAttachmentInfo depthAttachment{};
        if (rpe->deferredHasDepth)
            depthAttachment = ToVkRenderingAttachment(rpe->deferredDepthAtt);
        vk::RenderingAttachmentInfo stencilAttachment{};
        if (rpe->deferredHasStencil)
            stencilAttachment = ToVkRenderingAttachment(rpe->deferredStencilAtt);

        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea = vk::Rect2D{{0, 0},
                                              {rpe->deferredRenderWidth, rpe->deferredRenderHeight}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount =
            static_cast<uint32_t>(colorAttachments.size());
        renderingInfo.pColorAttachments = colorAttachments.data();
        if (rpe->deferredHasDepth)
            renderingInfo.pDepthAttachment = &depthAttachment;
        if (rpe->deferredHasStencil)
            renderingInfo.pStencilAttachment = &stencilAttachment;

        pe::GetVulkanCommandBuffer(rpe->cmd).beginRendering(renderingInfo);
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

    PeBarrierAccess BufferAccessForUsage(pwgpu::BufferUsageKind kind)
    {
        switch (kind)
        {
        case pwgpu::BufferUsageKind::Constant:
            return PE_ACCESS_UNIFORM_READ;
        case pwgpu::BufferUsageKind::StorageRead:
            return PE_ACCESS_SHADER_STORAGE_READ;
        case pwgpu::BufferUsageKind::Storage:
            return PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE;
        default:
            return PE_ACCESS_NONE;
        }
    }

    PeBarrierAccess TextureAccessForUsage(pwgpu::SubresourceUsageKind kind)
    {
        switch (kind)
        {
        case pwgpu::SubresourceUsageKind::Sampled:
            return PE_ACCESS_SHADER_SAMPLED_READ;
        case pwgpu::SubresourceUsageKind::ReadOnlyStorage:
            return PE_ACCESS_SHADER_STORAGE_READ;
        case pwgpu::SubresourceUsageKind::WriteOnlyStorage:
            return PE_ACCESS_SHADER_STORAGE_WRITE;
        case pwgpu::SubresourceUsageKind::ReadWriteStorage:
            return PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE;
        default:
            return PE_ACCESS_NONE;
        }
    }

    PeImageLayout TextureLayoutForUsage(pwgpu::SubresourceUsageKind kind)
    {
        return kind == pwgpu::SubresourceUsageKind::Sampled
                   ? PE_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                   : PE_IMAGE_LAYOUT_GENERAL;
    }

    void AppendBufferUsageBarrier(WGPUBufferImpl *buffer, uint8_t mask,
                                  std::vector<pe::BufferBarrierInfo> &bufferBarriers)
    {
        if (!buffer || !buffer->peBuffer)
            return;

        pe::BufferBarrierInfo barrier{};
        barrier.buffer = buffer->peBuffer;

        if (mask & static_cast<uint8_t>(pwgpu::BufferUsageKind::Storage))
        {
            barrier.stageMask |= PE_STAGE_ALL_GRAPHICS;
            barrier.accessMask |= PE_ACCESS_SHADER_STORAGE_READ | PE_ACCESS_SHADER_STORAGE_WRITE;
        }
        if (mask & static_cast<uint8_t>(pwgpu::BufferUsageKind::StorageRead))
        {
            barrier.stageMask |= PE_STAGE_ALL_GRAPHICS;
            barrier.accessMask |= PE_ACCESS_SHADER_STORAGE_READ;
        }
        if (mask & static_cast<uint8_t>(pwgpu::BufferUsageKind::Constant))
        {
            barrier.stageMask |= PE_STAGE_ALL_GRAPHICS;
            barrier.accessMask |= PE_ACCESS_UNIFORM_READ;
        }
        if (mask & static_cast<uint8_t>(pwgpu::BufferUsageKind::Input))
        {
            if (buffer->usage & WGPUBufferUsage_Indirect)
            {
                barrier.stageMask |= PE_STAGE_DRAW_INDIRECT;
                barrier.accessMask |= PE_ACCESS_INDIRECT_COMMAND_READ;
            }
            if (buffer->usage & WGPUBufferUsage_Index)
            {
                barrier.stageMask |= PE_STAGE_INDEX_INPUT;
                barrier.accessMask |= PE_ACCESS_INDEX_READ;
            }
            if (buffer->usage & WGPUBufferUsage_Vertex)
            {
                barrier.stageMask |= PE_STAGE_VERTEX_ATTRIBUTE_INPUT;
                barrier.accessMask |= PE_ACCESS_VERTEX_ATTRIBUTE_READ;
            }
        }

        if (barrier.stageMask && barrier.accessMask)
            bufferBarriers.push_back(barrier);
    }

    void AppendUsageScopeBarriers(const pwgpu::UsageScope &scope,
                                  std::vector<pe::ImageBarrierInfo> &imageBarriers,
                                  std::vector<pe::BufferBarrierInfo> &bufferBarriers)
    {
        for (const auto &entry : scope.map)
        {
            const auto &key = entry.first;
            auto *texture = key.texture;
            if (!texture || !texture->image)
                continue;

            const PeBarrierAccess access = TextureAccessForUsage(entry.second);
            if (!access)
                continue;

            pe::ImageBarrierInfo barrier{};
            barrier.image = texture->image;
            barrier.stageFlags = PE_STAGE_ALL_GRAPHICS;
            barrier.accessMask = access;
            barrier.layout = TextureLayoutForUsage(entry.second);
            barrier.baseMipLevel = key.mip;
            barrier.mipLevels = 1;
            barrier.baseArrayLayer =
                (texture->dimension == WGPUTextureDimension_3D) ? 0u : key.layer;
            barrier.arrayLayers = 1;
            imageBarriers.push_back(barrier);
        }

        for (const auto &entry : scope.bufferMap)
            AppendBufferUsageBarrier(entry.first, entry.second, bufferBarriers);
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
                barrier.stageFlags = PE_STAGE_ALL_GRAPHICS;
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
                barrier.stageMask = PE_STAGE_ALL_GRAPHICS;
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
        if (!pipeline || pipeline->invalid || pipeline->backendPipeline == 0)
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

        if (!pwgpu::BindWebGPURenderPipeline(rpe->cmd, pipeline))
        {
            rpe->invalid = true;
            return;
        }

        if (pipeline->layout)
            pwgpu::RebindWebGPUCompatibleBindGroups(
                rpe->cmd, pwgpu::PipelineBindingPoint::Render,
                pipeline->layout, rpe->currentBindGroups, &rpe->currentDynamicOffsets);
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
            if (rpe->currentDynamicOffsets.size() <= groupIndex)
                rpe->currentDynamicOffsets.resize(groupIndex + 1);
            if (dynamicOffsetCount > 0 && dynamicOffsets)
                rpe->currentDynamicOffsets[groupIndex].assign(
                    dynamicOffsets, dynamicOffsets + dynamicOffsetCount);
            else
                rpe->currentDynamicOffsets[groupIndex].clear();
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

        pwgpu::BindWebGPUBindGroup(rpe->cmd, pwgpu::PipelineBindingPoint::Render,
                                   rpe->pipeline->layout, groupIndex, group,
                                   dynamicOffsetCount, dynamicOffsets);
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

        rpe->cmd->BindVertexBuffer(buffer->peBuffer, static_cast<size_t>(offset), slot, 1);
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

        rpe->cmd->BindIndexBuffer(buffer->peBuffer, offset, pwgpu::ToPeIndexType(format));
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
        rpe->cmd->Draw(vertexCount, instanceCount, firstVertex, firstInstance);
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
        rpe->cmd->DrawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
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
        constexpr uint64_t kDrawArgsSize = PE_DRAW_INDIRECT_COMMAND_SIZE;
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
        rpe->cmd->DrawIndirect(buffer->peBuffer, static_cast<size_t>(offset), 1, PE_DRAW_INDIRECT_COMMAND_SIZE);
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
        constexpr uint64_t kDrawArgsSize = PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE;
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
        rpe->cmd->DrawIndexedIndirect(buffer->peBuffer, static_cast<size_t>(offset), 1,
                                      PE_DRAW_INDEXED_INDIRECT_COMMAND_SIZE);
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

        rpe->cmd->SetViewport(x, y, width, height, minDepth, maxDepth);
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

        rpe->cmd->SetScissor(static_cast<int32_t>(x), static_cast<int32_t>(y), width, height);
    }

    void wgpuRenderPassEncoderSetBlendConstant(WGPURenderPassEncoder rpe, WGPUColor const *color)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetBlendConstant"))
            return;
        if (!color)
            return;

        float constants[4] = {static_cast<float>(color->r), static_cast<float>(color->g),
                              static_cast<float>(color->b), static_cast<float>(color->a)};
        rpe->cmd->SetBlendConstants(constants);
    }

    void wgpuRenderPassEncoderSetStencilReference(WGPURenderPassEncoder rpe, uint32_t reference)
    {
        if (!RenderingActive(rpe, "wgpuRenderPassEncoderSetStencilReference"))
            return;

        rpe->cmd->SetStencilReference(reference & kStencilReferenceMask);
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
            rpe->occlusionQuerySet->backendQueryPool != 0 && rpe->occlusionQuerySet->count > 0)
        {
            pe::GetVulkanCommandBuffer(rpe->cmd).resetQueryPool(
                vk::QueryPool{QuerySetBackendQueryPoolHandle<VkQueryPool>(rpe->occlusionQuerySet)},
                0, rpe->occlusionQuerySet->count);
        }

        OpenRenderingIfNeeded(rpe);
        if (rpe->occlusionQuerySet->backendQueryPool != 0)
        {
            pe::GetVulkanCommandBuffer(rpe->cmd).beginQuery(
                vk::QueryPool{QuerySetBackendQueryPoolHandle<VkQueryPool>(rpe->occlusionQuerySet)},
                queryIndex, vk::QueryControlFlags{});
        }
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
        if (rpe->occlusionQuerySet && rpe->occlusionQuerySet->backendQueryPool != 0)
        {
            pe::GetVulkanCommandBuffer(rpe->cmd).endQuery(
                vk::QueryPool{QuerySetBackendQueryPoolHandle<VkQueryPool>(rpe->occlusionQuerySet)},
                lastIndex);
        }
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
            std::vector<pe::ImageBarrierInfo> bundleImageBarriers;
            std::vector<pe::BufferBarrierInfo> bundleBufferBarriers;
            for (size_t i = 0; i < bundleCount; i++)
            {
                auto *bundle = bundles[i];
                if (!bundle)
                    continue;
                AppendUsageScopeBarriers(bundle->usageScope, bundleImageBarriers,
                                         bundleBufferBarriers);
            }
            if (!bundleBufferBarriers.empty())
                rpe->cmd->BufferBarriers(bundleBufferBarriers);
            if (!bundleImageBarriers.empty())
                rpe->cmd->ImageBarriers(bundleImageBarriers);
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

            for (auto &command : bundle->commands)
                command(rpe->cmd);
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
            pwgpu::FireSyncValidation(rpe->device, "RenderPassEncoder.end(): pass is already ended");
            return;
        }
        if (rpe->parent && rpe->parent->finished)
        {
            pwgpu::FireSyncValidation(rpe->device,
                                      "RenderPassEncoder.end(): parent command encoder is already finished");
            rpe->ended = true;
            return;
        }
        if (!rpe->wasOpened)
        {
            pwgpu::FireSyncValidation(rpe->device,
                                      "RenderPassEncoder.end(): pass was never opened (invalid begin)");
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
            pe::GetVulkanCommandBuffer(rpe->cmd).endQuery(
                vk::QueryPool{QuerySetBackendQueryPoolHandle<VkQueryPool>(rpe->occlusionQuerySet)},
                lastIndex);
            rpe->occlusionQueryActive = false;
            rpe->activeOcclusionIndex = UINT32_MAX;
        }

        // Empty/valid passes still need begin/endRendering so loadOp/storeOp fire.
        // Skip if the pass is invalid-for-encoder to avoid emitting work for
        // never-meant-to-execute passes.
        const bool skipGpuWork =
            rpe->deferredResourceError ||
            (rpe->invalid && !rpe->deferredResourceError) ||
            !rpe->usageScopeValid;
        if (!rpe->renderingActive && !skipGpuWork)
            OpenRenderingIfNeeded(rpe);
        if (rpe->renderingActive)
        {
            pe::GetVulkanCommandBuffer(rpe->cmd).endRendering();
            rpe->renderingActive = false;
        }

        if (rpe->timestampQuerySet && rpe->endTimestampIndex != UINT32_MAX &&
            rpe->endTimestampIndex < rpe->timestampQuerySet->count)
        {
            pe::GetVulkanCommandBuffer(rpe->cmd).writeTimestamp2(
                vk::PipelineStageFlagBits2::eAllCommands,
                vk::QueryPool{QuerySetBackendQueryPoolHandle<VkQueryPool>(rpe->timestampQuerySet)},
                rpe->endTimestampIndex);
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
