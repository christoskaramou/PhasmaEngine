#include "CommandEncoder.h"
#include "RenderPass.h"
#include "ComputePass.h"
#include "RenderPipeline.h"
#include "ComputePipeline.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "Texture.h"
#include "QuerySet.h"
#include "Device.h"
#include "FormatMap.h"
#include "Utils.h"
#include "API/Image.h"
#include "API/Buffer.h"

extern "C" void wgpuRenderPipelineRelease(WGPURenderPipeline);
extern "C" void wgpuComputePipelineRelease(WGPUComputePipeline);
extern "C" void wgpuBindGroupRelease(WGPUBindGroup);
extern "C" void wgpuQuerySetAddRef(WGPUQuerySet);
extern "C" void wgpuQuerySetRelease(WGPUQuerySet);
extern "C" void wgpuTextureViewRelease(WGPUTextureView);
extern "C" void wgpuRenderBundleRelease(WGPURenderBundle);

void RetainedResources::MergeFrom(RetainedResources &other)
{
    renderPipelines.insert(renderPipelines.end(), other.renderPipelines.begin(), other.renderPipelines.end());
    computePipelines.insert(computePipelines.end(), other.computePipelines.begin(), other.computePipelines.end());
    bindGroups.insert(bindGroups.end(), other.bindGroups.begin(), other.bindGroups.end());
    querySets.insert(querySets.end(), other.querySets.begin(), other.querySets.end());
    textureViews.insert(textureViews.end(), other.textureViews.begin(), other.textureViews.end());
    renderBundles.insert(renderBundles.end(), other.renderBundles.begin(), other.renderBundles.end());
    usedBuffers.insert(usedBuffers.end(), other.usedBuffers.begin(), other.usedBuffers.end());
    other.renderPipelines.clear();
    other.computePipelines.clear();
    other.bindGroups.clear();
    other.querySets.clear();
    other.textureViews.clear();
    other.renderBundles.clear();
    other.usedBuffers.clear();
}

void RetainedResources::ReleaseAll()
{
    for (auto *p : renderPipelines)
        wgpuRenderPipelineRelease(p);
    for (auto *p : computePipelines)
        wgpuComputePipelineRelease(p);
    for (auto *bg : bindGroups)
        wgpuBindGroupRelease(bg);
    for (auto *qs : querySets)
        wgpuQuerySetRelease(qs);
    for (auto *tv : textureViews)
        wgpuTextureViewRelease(tv);
    for (auto *rb : renderBundles)
        wgpuRenderBundleRelease(rb);
    renderPipelines.clear();
    computePipelines.clear();
    bindGroups.clear();
    querySets.clear();
    textureViews.clear();
    renderBundles.clear();
}

namespace
{
    bool EncoderOpen(WGPUCommandEncoder enc, const char *apiName)
    {
        if (!enc || enc->finished)
        {
            PE_WARN("[WebGPU] %s: encoder is already finished", apiName);
            return false;
        }
        return true;
    }

    WGPUExtent3D MipExtent(const WGPUTextureImpl *tex, uint32_t mipLevel)
    {
        uint32_t w = std::max(1u, tex->size.width >> mipLevel);
        uint32_t h = std::max(1u, tex->size.height >> mipLevel);
        uint32_t d = (tex->dimension == WGPUTextureDimension_3D) ? std::max(1u, tex->size.depthOrArrayLayers >> mipLevel) : tex->size.depthOrArrayLayers;
        return {w, h, d};
    }

    vk::ImageAspectFlags AspectForCopy(WGPUTextureAspect aspect, WGPUTextureFormat fmt)
    {
        return pwgpu::ToVkAspect(aspect, fmt);
    }

    bool ValidateTextureCopyRange(const WGPUTextureImpl *tex, uint32_t mipLevel,
                                  const WGPUOrigin3D &origin, const WGPUExtent3D &copySize)
    {
        if (mipLevel >= tex->mipLevelCount)
            return false;
        WGPUExtent3D mip = MipExtent(tex, mipLevel);
        uint32_t blockW, blockH;
        pwgpu::GetTexelBlockSize(tex->format, blockW, blockH);
        if (origin.x + copySize.width > mip.width)
            return false;
        if (origin.y + copySize.height > mip.height)
            return false;
        if (tex->dimension == WGPUTextureDimension_3D)
        {
            if (origin.z + copySize.depthOrArrayLayers > mip.depthOrArrayLayers)
                return false;
        }
        else
        {
            if (origin.z + copySize.depthOrArrayLayers > tex->size.depthOrArrayLayers)
                return false;
        }
        if (blockW > 1 || blockH > 1)
        {
            if (origin.x % blockW != 0 || origin.y % blockH != 0)
                return false;
            if (origin.x + copySize.width != mip.width && copySize.width % blockW != 0)
                return false;
            if (origin.y + copySize.height != mip.height && copySize.height % blockH != 0)
                return false;
        }
        return true;
    }

    bool ValidateBufferCopyLayout(uint64_t bufferSize, uint64_t offset,
                                  uint32_t bytesPerRow, uint32_t rowsPerImage,
                                  const WGPUExtent3D &copySize,
                                  uint32_t footprint, uint32_t blockW, uint32_t blockH)
    {
        if (footprint == 0)
            return false;
        uint32_t widthInBlocks = (copySize.width + blockW - 1) / blockW;
        uint32_t heightInBlocks = (copySize.height + blockH - 1) / blockH;
        uint32_t minBytesPerRow = widthInBlocks * footprint;
        if (bytesPerRow < minBytesPerRow)
            return false;
        uint32_t copyDepth = copySize.depthOrArrayLayers;
        if (copyDepth == 0)
            return true;
        uint64_t bytesPerImage = static_cast<uint64_t>(bytesPerRow) * rowsPerImage;
        uint64_t lastRowBytes = static_cast<uint64_t>(bytesPerRow) * (heightInBlocks - 1) + minBytesPerRow;
        uint64_t required = offset + (copyDepth > 1 ? bytesPerImage * (copyDepth - 1) : 0) + lastRowBytes;
        return required <= bufferSize;
    }
} // namespace

extern "C"
{

    void wgpuCommandEncoderAddRef(WGPUCommandEncoder enc)
    {
        if (enc)
            enc->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuCommandEncoderRelease(WGPUCommandEncoder enc)
    {
        if (!enc)
            return;
        if (enc->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            enc->retained.ReleaseAll();
            if (enc->cmd)
            {
                enc->cmd->End();
                enc->cmd->Return();
            }
            delete enc;
        }
    }

    WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder enc,
                                                            WGPURenderPassDescriptor const *descriptor)
    {
        if (!enc || enc->finished || enc->hasOpenPass)
            return nullptr;
        if (!descriptor)
            return nullptr;

        size_t colorCount = descriptor->colorAttachmentCount;
        uint32_t maxColorAttachments = enc->device ? enc->device->limits.maxColorAttachments : 8;
        if (colorCount > maxColorAttachments)
        {
            PE_WARN("[WebGPU] beginRenderPass: colorAttachmentCount %zu > maxColorAttachments %u",
                    colorCount, maxColorAttachments);
            return nullptr;
        }

        bool hasAnyColorAttachment = false;
        for (size_t i = 0; i < colorCount; i++)
        {
            if (descriptor->colorAttachments[i].view != nullptr)
            {
                hasAnyColorAttachment = true;
                break;
            }
        }
        if (!hasAnyColorAttachment && !descriptor->depthStencilAttachment)
        {
            PE_WARN("[WebGPU] beginRenderPass: must have at least one color or depth/stencil attachment");
            return nullptr;
        }

        uint32_t attachW = 0, attachH = 0;
        uint32_t commonSampleCount = 0;

        for (size_t i = 0; i < colorCount; i++)
        {
            auto &ca = descriptor->colorAttachments[i];
            if (!ca.view)
                continue;

            auto *view = ca.view;
            if (!view->texture)
            {
                enc->invalid = true;
                return nullptr;
            }
            if (view->texture->destroyed)
            {
                view->refCount.fetch_add(1, std::memory_order_relaxed);
                enc->retained.textureViews.push_back(view);
                return nullptr;
            }
            if (!view->view || view->texture->invalid)
            {
                enc->invalid = true;
                return nullptr;
            }

            if (pwgpu::IsDepthStencilFormat(view->format) || !pwgpu::IsRenderableFormat(view->format))
            {
                PE_WARN("[WebGPU] beginRenderPass: color attachment %zu format is not color-renderable", i);
                return nullptr;
            }

            if (!(view->usage & WGPUTextureUsage_RenderAttachment))
            {
                PE_WARN("[WebGPU] beginRenderPass: color attachment %zu view missing RENDER_ATTACHMENT usage", i);
                enc->invalid = true;
            }

            if (view->mipLevelCount != 1)
            {
                PE_WARN("[WebGPU] beginRenderPass: color attachment %zu mipLevelCount must be 1", i);
                return nullptr;
            }
            if (view->arrayLayerCount != 1)
            {
                PE_WARN("[WebGPU] beginRenderPass: color attachment %zu arrayLayerCount must be 1", i);
                return nullptr;
            }

            if (attachW == 0)
            {
                attachW = view->renderExtent.width;
                attachH = view->renderExtent.height;
            }
            else if (view->renderExtent.width != attachW || view->renderExtent.height != attachH)
            {
                PE_WARN("[WebGPU] beginRenderPass: color attachment %zu renderExtent mismatch", i);
                return nullptr;
            }

            uint32_t sc = view->texture->sampleCount;
            if (commonSampleCount == 0)
                commonSampleCount = sc;
            else if (sc != commonSampleCount)
            {
                PE_WARN("[WebGPU] beginRenderPass: color attachment %zu sampleCount mismatch", i);
                return nullptr;
            }

            if (view->dimension == WGPUTextureViewDimension_3D)
            {
                if (ca.depthSlice == WGPU_DEPTH_SLICE_UNDEFINED)
                {
                    PE_WARN("[WebGPU] beginRenderPass: 3D color attachment %zu requires depthSlice", i);
                    return nullptr;
                }
            }
            else
            {
                if (ca.depthSlice != WGPU_DEPTH_SLICE_UNDEFINED)
                {
                    PE_WARN("[WebGPU] beginRenderPass: non-3D color attachment %zu must not have depthSlice", i);
                    return nullptr;
                }
            }

            if (ca.resolveTarget)
            {
                auto *rt = ca.resolveTarget;
                if (!rt->view || !rt->texture || rt->texture->invalid || rt->texture->destroyed)
                {
                    PE_WARN("[WebGPU] beginRenderPass: color attachment %zu resolveTarget is invalid", i);
                    return nullptr;
                }
                if (view->texture->sampleCount <= 1)
                {
                    PE_WARN("[WebGPU] beginRenderPass: resolveTarget on non-MSAA attachment %zu", i);
                    return nullptr;
                }
                if (rt->texture->sampleCount != 1)
                {
                    PE_WARN("[WebGPU] beginRenderPass: resolveTarget sampleCount must be 1 at attachment %zu", i);
                    return nullptr;
                }
                if (rt->format != view->format)
                {
                    PE_WARN("[WebGPU] beginRenderPass: resolveTarget format mismatch at attachment %zu", i);
                    return nullptr;
                }
                if (rt->renderExtent.width != view->renderExtent.width ||
                    rt->renderExtent.height != view->renderExtent.height)
                {
                    PE_WARN("[WebGPU] beginRenderPass: resolveTarget renderExtent mismatch at attachment %zu", i);
                    return nullptr;
                }
                if (!(rt->usage & WGPUTextureUsage_RenderAttachment))
                {
                    PE_WARN("[WebGPU] beginRenderPass: resolveTarget missing RENDER_ATTACHMENT usage at %zu", i);
                    return nullptr;
                }
                if (rt->dimension == WGPUTextureViewDimension_3D)
                {
                    PE_WARN("[WebGPU] beginRenderPass: resolveTarget must not be a 3D view at %zu", i);
                    return nullptr;
                }
                if (rt->mipLevelCount != 1 || rt->arrayLayerCount != 1)
                {
                    PE_WARN("[WebGPU] beginRenderPass: resolveTarget must be single mip/layer at %zu", i);
                    return nullptr;
                }
            }

            if (ca.loadOp != WGPULoadOp_Clear && ca.loadOp != WGPULoadOp_Load)
            {
                PE_WARN("[WebGPU] beginRenderPass: color attachment %zu invalid loadOp", i);
                return nullptr;
            }
            if (ca.storeOp != WGPUStoreOp_Store && ca.storeOp != WGPUStoreOp_Discard)
            {
                PE_WARN("[WebGPU] beginRenderPass: color attachment %zu invalid storeOp", i);
                return nullptr;
            }
        }

        auto *dsa = descriptor->depthStencilAttachment;
        if (dsa)
        {
            if (!dsa->view || !dsa->view->texture)
            {
                enc->invalid = true;
                return nullptr;
            }
            if (dsa->view->texture->destroyed)
            {
                dsa->view->refCount.fetch_add(1, std::memory_order_relaxed);
                enc->retained.textureViews.push_back(dsa->view);
                return nullptr;
            }
            if (!dsa->view->view || dsa->view->texture->invalid)
            {
                enc->invalid = true;
                return nullptr;
            }

            auto *dsView = dsa->view;
            if (!pwgpu::IsDepthStencilFormat(dsView->format))
            {
                PE_WARN("[WebGPU] beginRenderPass: depthStencilAttachment format is not depth-stencil");
                return nullptr;
            }

            if (!(dsView->usage & WGPUTextureUsage_RenderAttachment))
            {
                PE_WARN("[WebGPU] beginRenderPass: depthStencilAttachment missing RENDER_ATTACHMENT usage");
                return nullptr;
            }

            if (dsView->mipLevelCount != 1)
            {
                PE_WARN("[WebGPU] beginRenderPass: depthStencilAttachment mipLevelCount must be 1");
                return nullptr;
            }
            if (dsView->arrayLayerCount != 1)
            {
                PE_WARN("[WebGPU] beginRenderPass: depthStencilAttachment arrayLayerCount must be 1");
                return nullptr;
            }

            if (attachW == 0)
            {
                attachW = dsView->renderExtent.width;
                attachH = dsView->renderExtent.height;
            }
            else if (dsView->renderExtent.width != attachW || dsView->renderExtent.height != attachH)
            {
                PE_WARN("[WebGPU] beginRenderPass: depthStencilAttachment renderExtent mismatch");
                return nullptr;
            }

            uint32_t sc = dsView->texture->sampleCount;
            if (commonSampleCount == 0)
                commonSampleCount = sc;
            else if (sc != commonSampleCount)
            {
                PE_WARN("[WebGPU] beginRenderPass: depthStencilAttachment sampleCount mismatch");
                return nullptr;
            }

            bool hasDepth = pwgpu::HasDepthAspect(dsView->format);
            bool hasStencil = pwgpu::HasStencilAspect(dsView->format);

            if (hasDepth && !dsa->depthReadOnly)
            {
                if (dsa->depthLoadOp != WGPULoadOp_Clear && dsa->depthLoadOp != WGPULoadOp_Load)
                {
                    PE_WARN("[WebGPU] beginRenderPass: depthLoadOp must be provided when depth is writable");
                    enc->invalid = true;
                }
                if (dsa->depthStoreOp != WGPUStoreOp_Store && dsa->depthStoreOp != WGPUStoreOp_Discard)
                {
                    PE_WARN("[WebGPU] beginRenderPass: depthStoreOp must be provided when depth is writable");
                    enc->invalid = true;
                }
            }
            if (hasDepth && dsa->depthLoadOp == WGPULoadOp_Clear)
            {
                if (dsa->depthClearValue < 0.0f || dsa->depthClearValue > 1.0f)
                {
                    PE_WARN("[WebGPU] beginRenderPass: depthClearValue must be in [0, 1]");
                    return nullptr;
                }
            }

            if (hasStencil && !dsa->stencilReadOnly)
            {
                if (dsa->stencilLoadOp != WGPULoadOp_Clear && dsa->stencilLoadOp != WGPULoadOp_Load)
                {
                    PE_WARN("[WebGPU] beginRenderPass: stencilLoadOp must be provided when stencil is writable");
                    enc->invalid = true;
                }
                if (dsa->stencilStoreOp != WGPUStoreOp_Store && dsa->stencilStoreOp != WGPUStoreOp_Discard)
                {
                    PE_WARN("[WebGPU] beginRenderPass: stencilStoreOp must be provided when stencil is writable");
                    enc->invalid = true;
                }
            }
        }

        if (descriptor->occlusionQuerySet)
        {
            auto *oqs = descriptor->occlusionQuerySet;
            if (oqs->destroyed || oqs->queryPool == VK_NULL_HANDLE)
            {
                PE_WARN("[WebGPU] beginRenderPass: occlusionQuerySet is destroyed or invalid");
                return nullptr;
            }
            if (oqs->type != WGPUQueryType_Occlusion)
            {
                PE_WARN("[WebGPU] beginRenderPass: occlusionQuerySet must be of type Occlusion");
                return nullptr;
            }
        }

        if (descriptor->timestampWrites)
        {
            auto *tw = descriptor->timestampWrites;
            if (!tw->querySet || tw->querySet->type != WGPUQueryType_Timestamp ||
                tw->querySet->queryPool == VK_NULL_HANDLE || tw->querySet->destroyed)
            {
                PE_WARN("[WebGPU] beginRenderPass: timestampWrites querySet is invalid");
                return nullptr;
            }

            bool hasBegin = (tw->beginningOfPassWriteIndex != WGPU_QUERY_SET_INDEX_UNDEFINED);
            bool hasEnd = (tw->endOfPassWriteIndex != WGPU_QUERY_SET_INDEX_UNDEFINED);

            if (!hasBegin && !hasEnd)
            {
                PE_WARN("[WebGPU] beginRenderPass: timestampWrites must have at least one write index");
                return nullptr;
            }
            if (hasBegin && tw->beginningOfPassWriteIndex >= tw->querySet->count)
            {
                PE_WARN("[WebGPU] beginRenderPass: beginningOfPassWriteIndex out of range");
                return nullptr;
            }
            if (hasEnd && tw->endOfPassWriteIndex >= tw->querySet->count)
            {
                PE_WARN("[WebGPU] beginRenderPass: endOfPassWriteIndex out of range");
                return nullptr;
            }
            if (hasBegin && hasEnd && tw->beginningOfPassWriteIndex == tw->endOfPassWriteIndex)
            {
                PE_WARN("[WebGPU] beginRenderPass: begin and end timestamp indices must differ");
                return nullptr;
            }
        }

        auto *rpe = new WGPURenderPassEncoderImpl();
        rpe->cmd = enc->cmd;
        rpe->parent = enc;
        rpe->device = enc->device;
        rpe->attachmentWidth = attachW;
        rpe->attachmentHeight = attachH;
        if (descriptor->label.data)
            rpe->label = pwgpu::ToString(descriptor->label);

        if (dsa)
        {
            rpe->depthReadOnly = dsa->depthReadOnly;
            rpe->stencilReadOnly = dsa->stencilReadOnly;
            rpe->depthStencilFormat = dsa->view->format;
        }

        rpe->sampleCount = commonSampleCount ? commonSampleCount : 1;
        rpe->colorFormats.reserve(colorCount);
        for (size_t i = 0; i < colorCount; i++)
        {
            auto &ca = descriptor->colorAttachments[i];
            rpe->colorFormats.push_back(ca.view ? ca.view->format : WGPUTextureFormat_Undefined);
        }

        if (descriptor->occlusionQuerySet)
        {
            rpe->occlusionQuerySet = descriptor->occlusionQuerySet;
            wgpuQuerySetAddRef(descriptor->occlusionQuerySet);

            bool alreadyReset = false;
            for (auto *qs : enc->resetOcclusionQuerySets)
            {
                if (qs == descriptor->occlusionQuerySet)
                {
                    alreadyReset = true;
                    break;
                }
            }
            if (!alreadyReset)
            {
                enc->cmd->ApiHandle().resetQueryPool(
                    descriptor->occlusionQuerySet->queryPool, 0, descriptor->occlusionQuerySet->count);
                enc->resetOcclusionQuerySets.push_back(descriptor->occlusionQuerySet);
            }
        }

        if (descriptor->timestampWrites)
        {
            auto *tw = descriptor->timestampWrites;
            rpe->timestampQuerySet = tw->querySet;
            wgpuQuerySetAddRef(tw->querySet);

            if (tw->beginningOfPassWriteIndex < tw->querySet->count)
            {
                rpe->beginTimestampIndex = tw->beginningOfPassWriteIndex;
                enc->cmd->ApiHandle().writeTimestamp2(
                    vk::PipelineStageFlagBits2::eAllCommands,
                    tw->querySet->queryPool, tw->beginningOfPassWriteIndex);
            }
            if (tw->endOfPassWriteIndex < tw->querySet->count)
                rpe->endTimestampIndex = tw->endOfPassWriteIndex;
        }

        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        std::vector<pe::ImageBarrierInfo> barriers;
        colorAttachments.reserve(colorCount);

        for (size_t i = 0; i < colorCount; i++)
        {
            auto &ca = descriptor->colorAttachments[i];
            if (!ca.view)
            {
                vk::RenderingAttachmentInfo nullAtt{};
                nullAtt.imageView = VK_NULL_HANDLE;
                nullAtt.imageLayout = vk::ImageLayout::eUndefined;
                nullAtt.loadOp = vk::AttachmentLoadOp::eDontCare;
                nullAtt.storeOp = vk::AttachmentStoreOp::eDontCare;
                colorAttachments.push_back(nullAtt);
                continue;
            }

            auto *view = ca.view;

            // For 3D views, create a temporary 2D slice view targeting the specific depthSlice.
            // Vulkan dynamic rendering requires a 2D view; a 3D VkImageView is not valid as a
            // color attachment. The image must have VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT.
            vk::ImageView attachmentImageView = view->view->ApiHandle();
            pe::ImageView *sliceView = nullptr;
            uint32_t barrierBaseLayer = view->baseArrayLayer;

            if (view->dimension == WGPUTextureViewDimension_3D &&
                ca.depthSlice != WGPU_DEPTH_SLICE_UNDEFINED)
            {
                vk::ImageViewCreateInfo sliceIvci = pe::ImageView::CreateInfoInit();
                sliceIvci.image = view->texture->image->ApiHandle();
                sliceIvci.viewType = vk::ImageViewType::e2D;
                sliceIvci.format = static_cast<vk::Format>(pwgpu::ToVkFormat(view->format));
                sliceIvci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
                sliceIvci.subresourceRange.baseMipLevel = view->baseMipLevel;
                sliceIvci.subresourceRange.levelCount = 1;
                sliceIvci.subresourceRange.baseArrayLayer = ca.depthSlice;
                sliceIvci.subresourceRange.layerCount = 1;

                try
                {
                    sliceView = pe::ImageView::Create(view->texture->image, sliceIvci, "wgpu_3d_slice");
                }
                catch (...)
                {
                    PE_WARN("[WebGPU] beginRenderPass: failed to create 2D slice view for 3D attachment %zu", i);
                }

                if (sliceView)
                {
                    attachmentImageView = sliceView->ApiHandle();
                    rpe->ownedSliceViews.push_back(sliceView);
                    barrierBaseLayer = ca.depthSlice;
                }
            }

            pe::ImageBarrierInfo barrier{};
            barrier.image = view->texture->image;
            barrier.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.accessMask = vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead;
            barrier.layout = vk::ImageLayout::eColorAttachmentOptimal;
            barrier.baseMipLevel = view->baseMipLevel;
            barrier.mipLevels = 1;
            barrier.baseArrayLayer = barrierBaseLayer;
            barrier.arrayLayers = 1;
            barriers.push_back(barrier);

            view->refCount.fetch_add(1, std::memory_order_relaxed);
            rpe->retainedViews.push_back(view);

            {
                std::string err;
                if (!rpe->usageScope.AddView(view, pwgpu::SubresourceUsageKind::Attachment, err))
                    rpe->usageScopeValid = false;
            }

            vk::RenderingAttachmentInfo att{};
            att.imageView = attachmentImageView;
            att.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            att.loadOp = (ca.loadOp == WGPULoadOp_Clear) ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            att.storeOp = (ca.storeOp == WGPUStoreOp_Store) ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;

            if (ca.loadOp == WGPULoadOp_Clear)
            {
                att.clearValue.color.float32[0] = static_cast<float>(ca.clearValue.r);
                att.clearValue.color.float32[1] = static_cast<float>(ca.clearValue.g);
                att.clearValue.color.float32[2] = static_cast<float>(ca.clearValue.b);
                att.clearValue.color.float32[3] = static_cast<float>(ca.clearValue.a);
            }

            if (ca.resolveTarget)
            {
                auto *rt = ca.resolveTarget;

                pe::ImageBarrierInfo resolveBarrier{};
                resolveBarrier.image = rt->texture->image;
                resolveBarrier.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
                resolveBarrier.accessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
                resolveBarrier.layout = vk::ImageLayout::eColorAttachmentOptimal;
                resolveBarrier.baseMipLevel = rt->baseMipLevel;
                resolveBarrier.mipLevels = 1;
                resolveBarrier.baseArrayLayer = rt->baseArrayLayer;
                resolveBarrier.arrayLayers = rt->arrayLayerCount;
                barriers.push_back(resolveBarrier);

                rt->refCount.fetch_add(1, std::memory_order_relaxed);
                rpe->retainedViews.push_back(rt);

                att.resolveMode = pwgpu::IsBlendableFormat(view->format)
                                      ? vk::ResolveModeFlagBits::eAverage
                                      : vk::ResolveModeFlagBits::eSampleZero;
                att.resolveImageView = rt->view->ApiHandle();
                att.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            }

            colorAttachments.push_back(att);
        }

        vk::RenderingAttachmentInfo depthAtt{};
        vk::RenderingAttachmentInfo stencilAtt{};
        bool hasDepthAttachment = false;
        bool hasStencilAttachment = false;
        if (dsa)
        {
            auto *dsView = dsa->view;
            bool hasDepth = pwgpu::HasDepthAspect(dsView->format);
            bool hasStencil = pwgpu::HasStencilAspect(dsView->format);

            vk::ImageLayout dsLayout = (dsa->depthReadOnly && dsa->stencilReadOnly)
                                           ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                                       : (dsa->depthReadOnly && hasStencil)
                                           ? vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal
                                       : (dsa->stencilReadOnly && hasDepth)
                                           ? vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal
                                           : vk::ImageLayout::eDepthStencilAttachmentOptimal;

            if (hasDepth && !hasStencil)
                dsLayout = dsa->depthReadOnly ? vk::ImageLayout::eDepthReadOnlyOptimal
                                              : vk::ImageLayout::eDepthAttachmentOptimal;
            else if (hasStencil && !hasDepth)
                dsLayout = dsa->stencilReadOnly ? vk::ImageLayout::eStencilReadOnlyOptimal
                                                : vk::ImageLayout::eStencilAttachmentOptimal;

            pe::ImageBarrierInfo dsBarrier{};
            dsBarrier.image = dsView->texture->image;
            dsBarrier.stageFlags = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            dsBarrier.accessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead;
            if (!dsa->depthReadOnly)
                dsBarrier.accessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            if (!dsa->stencilReadOnly)
                dsBarrier.accessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            dsBarrier.layout = dsLayout;
            dsBarrier.baseMipLevel = dsView->baseMipLevel;
            dsBarrier.mipLevels = 1;
            dsBarrier.baseArrayLayer = dsView->baseArrayLayer;
            dsBarrier.arrayLayers = dsView->arrayLayerCount;
            barriers.push_back(dsBarrier);

            dsView->refCount.fetch_add(1, std::memory_order_relaxed);
            rpe->retainedViews.push_back(dsView);

            {
                auto addAspect = [&](WGPUTextureAspect aspect, bool readOnly)
                {
                    pwgpu::SubresourceKey key{};
                    key.texture = dsView->texture;
                    key.aspect = static_cast<uint32_t>(aspect);
                    auto kind = readOnly ? pwgpu::SubresourceUsageKind::AttachmentReadOnly
                                         : pwgpu::SubresourceUsageKind::Attachment;
                    const uint32_t mipEnd = dsView->baseMipLevel + dsView->mipLevelCount;
                    const uint32_t layerEnd = dsView->baseArrayLayer + dsView->arrayLayerCount;
                    for (uint32_t m = dsView->baseMipLevel; m < mipEnd; ++m)
                    {
                        for (uint32_t l = dsView->baseArrayLayer; l < layerEnd; ++l)
                        {
                            key.mip = m;
                            key.layer = l;
                            std::string err;
                            if (!rpe->usageScope.AddSubresource(key, kind, err))
                                rpe->usageScopeValid = false;
                        }
                    }
                };
                if (hasDepth)
                    addAspect(WGPUTextureAspect_DepthOnly, dsa->depthReadOnly);
                if (hasStencil)
                    addAspect(WGPUTextureAspect_StencilOnly, dsa->stencilReadOnly);
            }

            if (hasDepth)
            {
                hasDepthAttachment = true;
                depthAtt.imageView = dsView->view->ApiHandle();
                depthAtt.imageLayout = dsLayout;

                if (!dsa->depthReadOnly)
                {
                    depthAtt.loadOp = (dsa->depthLoadOp == WGPULoadOp_Clear) ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                    depthAtt.storeOp = (dsa->depthStoreOp == WGPUStoreOp_Store) ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
                }
                else
                {
                    depthAtt.loadOp = vk::AttachmentLoadOp::eLoad;
                    depthAtt.storeOp = vk::AttachmentStoreOp::eNone;
                }

                if (dsa->depthLoadOp == WGPULoadOp_Clear)
                    depthAtt.clearValue.depthStencil.depth = dsa->depthClearValue;
            }

            if (hasStencil)
            {
                hasStencilAttachment = true;
                stencilAtt.imageView = dsView->view->ApiHandle();
                stencilAtt.imageLayout = dsLayout;

                if (!dsa->stencilReadOnly)
                {
                    stencilAtt.loadOp = (dsa->stencilLoadOp == WGPULoadOp_Clear) ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                    stencilAtt.storeOp = (dsa->stencilStoreOp == WGPUStoreOp_Store) ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
                }
                else
                {
                    stencilAtt.loadOp = vk::AttachmentLoadOp::eLoad;
                    stencilAtt.storeOp = vk::AttachmentStoreOp::eNone;
                }

                if (dsa->stencilLoadOp == WGPULoadOp_Clear)
                    stencilAtt.clearValue.depthStencil.stencil = dsa->stencilClearValue;
            }
        }

        if (!barriers.empty())
            enc->cmd->ImageBarriers(barriers);

        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea = vk::Rect2D{{0, 0}, {attachW, attachH}};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        renderingInfo.pColorAttachments = colorAttachments.data();
        if (hasDepthAttachment)
            renderingInfo.pDepthAttachment = &depthAtt;
        if (hasStencilAttachment)
            renderingInfo.pStencilAttachment = &stencilAtt;

        enc->cmd->ApiHandle().beginRendering(renderingInfo);
        rpe->renderingActive = true;

        vk::Viewport defaultVp{0.0f, 0.0f, static_cast<float>(attachW), static_cast<float>(attachH), 0.0f, 1.0f};
        enc->cmd->ApiHandle().setViewport(0, 1, &defaultVp);

        vk::Rect2D defaultScissor{{0, 0}, {attachW, attachH}};
        enc->cmd->ApiHandle().setScissor(0, 1, &defaultScissor);

        enc->hasOpenPass = true;
        return rpe;
    }

    WGPUComputePassEncoder wgpuCommandEncoderBeginComputePass(WGPUCommandEncoder enc,
                                                              WGPUComputePassDescriptor const *descriptor)
    {
        if (!enc || enc->finished || enc->hasOpenPass)
            return nullptr;
        auto *cpe = new WGPUComputePassEncoderImpl();
        cpe->cmd = enc->cmd;
        cpe->parent = enc;
        cpe->device = enc->device;
        if (descriptor)
        {
            if (descriptor->label.data)
                cpe->label = pwgpu::ToString(descriptor->label);

            if (descriptor->timestampWrites)
            {
                auto *tw = descriptor->timestampWrites;
                if (tw->querySet && tw->querySet->type == WGPUQueryType_Timestamp &&
                    tw->querySet->queryPool != VK_NULL_HANDLE && !tw->querySet->destroyed)
                {
                    cpe->timestampQuerySet = tw->querySet;
                    wgpuQuerySetAddRef(tw->querySet);
                    if (tw->beginningOfPassWriteIndex < tw->querySet->count)
                    {
                        cpe->beginTimestampIndex = tw->beginningOfPassWriteIndex;
                        enc->cmd->ApiHandle().writeTimestamp2(
                            vk::PipelineStageFlagBits2::eAllCommands,
                            tw->querySet->queryPool, tw->beginningOfPassWriteIndex);
                    }
                    if (tw->endOfPassWriteIndex < tw->querySet->count)
                        cpe->endTimestampIndex = tw->endOfPassWriteIndex;
                }
            }
        }
        enc->hasOpenPass = true;
        return cpe;
    }

    WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder enc,
                                               WGPUCommandBufferDescriptor const *descriptor)
    {
        if (!enc || enc->finished || enc->hasOpenPass)
            return nullptr;
        enc->finished = true;

        if (enc->cmd)
            enc->cmd->End();

        auto *cb = new WGPUCommandBufferImpl();
        cb->device = enc->device;
        cb->cmd = enc->cmd;
        enc->cmd = nullptr;
        cb->invalid = enc->invalid;

        cb->retained.MergeFrom(enc->retained);

        if (descriptor && descriptor->label.data)
            cb->label = pwgpu::ToString(descriptor->label);

        if (enc->invalid && enc->device)
            enc->device->reportError(WGPUErrorType_Validation,
                                     pwgpu::ToStringView("CommandEncoder.finish(): encoder is invalid"));
        return cb;
    }

    void wgpuCommandEncoderCopyBufferToBuffer(WGPUCommandEncoder enc,
                                              WGPUBuffer src, uint64_t srcOffset,
                                              WGPUBuffer dst, uint64_t dstOffset,
                                              uint64_t size)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyBufferToBuffer"))
            return;

        auto fail = [&](const char *msg)
        {
            (void)msg;
            enc->invalid = true;
        };

        if (!src || !dst)
        {
            fail("src or dst buffer is null");
            return;
        }
        if (src->invalid || dst->invalid)
        {
            fail("src or dst buffer is invalid");
            return;
        }
        if (src->device != enc->device || dst->device != enc->device)
        {
            fail("src/dst buffer belongs to a different device");
            return;
        }
        if (src == dst)
        {
            fail("src and dst must be different buffers");
            return;
        }
        if (!(src->usage & WGPUBufferUsage_CopySrc))
        {
            fail("src usage must include COPY_SRC");
            return;
        }
        if (!(dst->usage & WGPUBufferUsage_CopyDst))
        {
            fail("dst usage must include COPY_DST");
            return;
        }
        if (srcOffset > src->size || dstOffset > dst->size)
        {
            fail("offset exceeds buffer size");
            return;
        }

        uint64_t copySize = size;
        if (copySize == WGPU_WHOLE_SIZE)
            copySize = src->size - srcOffset;

        if (srcOffset % 4 != 0 || dstOffset % 4 != 0 || copySize % 4 != 0)
        {
            fail("offsets and size must be multiples of 4");
            return;
        }
        if (copySize > src->size - srcOffset ||
            copySize > dst->size - dstOffset)
        {
            fail("copy range out of bounds");
            return;
        }
        if (!src->peBuffer || !dst->peBuffer)
        {
            enc->retained.usedBuffers.push_back(src);
            enc->retained.usedBuffers.push_back(dst);
            return;
        }

        vk::MemoryBarrier2 mb{};
        mb.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        mb.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
        mb.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        mb.dstAccessMask = vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite;
        enc->cmd->MemoryBarrier(mb);

        enc->cmd->CopyBuffer(src->peBuffer, dst->peBuffer, static_cast<size_t>(copySize),
                             static_cast<size_t>(srcOffset), static_cast<size_t>(dstOffset));
        enc->retained.usedBuffers.push_back(src);
        enc->retained.usedBuffers.push_back(dst);
    }

    void wgpuCommandEncoderClearBuffer(WGPUCommandEncoder enc,
                                       WGPUBuffer buffer,
                                       uint64_t offset,
                                       uint64_t size)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderClearBuffer"))
            return;
        if (!buffer || !buffer->peBuffer ||
            buffer->internalState == BufferInternalState::Destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_CopyDst))
            return;

        if (offset > buffer->size)
            return;

        uint64_t clearSize = size;
        if (clearSize == WGPU_WHOLE_SIZE)
            clearSize = buffer->size - offset;

        if (offset % 4 != 0)
            return;
        if (clearSize % 4 != 0)
            return;
        if (offset + clearSize > buffer->size)
            return;

        enc->cmd->FillBuffer(buffer->peBuffer, static_cast<size_t>(offset),
                             static_cast<size_t>(clearSize), 0);
        enc->retained.usedBuffers.push_back(buffer);
    }

    void wgpuCommandEncoderCopyBufferToTexture(WGPUCommandEncoder enc,
                                               WGPUTexelCopyBufferInfo const *src,
                                               WGPUTexelCopyTextureInfo const *dst,
                                               WGPUExtent3D const *copySize)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyBufferToTexture"))
            return;
        if (!src || !src->buffer || !src->buffer->peBuffer ||
            src->buffer->internalState == BufferInternalState::Destroyed)
            return;
        if (!dst || !dst->texture || !dst->texture->image || dst->texture->destroyed)
            return;
        if (!copySize)
            return;
        if (!(src->buffer->usage & WGPUBufferUsage_CopySrc))
            return;
        if (!(dst->texture->usage & WGPUTextureUsage_CopyDst))
            return;

        if (src->layout.bytesPerRow % 256 != 0)
            return;
        if (!ValidateTextureCopyRange(dst->texture, dst->mipLevel, dst->origin, *copySize))
            return;

        WGPUTextureAspect aspect = dst->aspect;
        vk::ImageAspectFlags vkAspect = AspectForCopy(aspect, dst->texture->format);

        uint32_t blockW, blockH;
        pwgpu::GetTexelBlockSize(dst->texture->format, blockW, blockH);
        uint32_t footprint = pwgpu::TexelBlockCopyFootprint(dst->texture->format, aspect);

        if (!ValidateBufferCopyLayout(src->buffer->size, src->layout.offset,
                                      src->layout.bytesPerRow, src->layout.rowsPerImage,
                                      *copySize, footprint, blockW, blockH))
            return;

        vk::BufferImageCopy2 region{};
        region.bufferOffset = src->layout.offset;
        region.bufferRowLength = (footprint > 0) ? (src->layout.bytesPerRow / footprint) * blockW : 0;
        region.bufferImageHeight = src->layout.rowsPerImage * blockH;
        region.imageSubresource.aspectMask = vkAspect;
        region.imageSubresource.mipLevel = dst->mipLevel;
        region.imageSubresource.baseArrayLayer = (dst->texture->dimension == WGPUTextureDimension_3D) ? 0 : dst->origin.z;
        region.imageSubresource.layerCount = (dst->texture->dimension == WGPUTextureDimension_3D) ? 1 : copySize->depthOrArrayLayers;
        region.imageOffset = vk::Offset3D{static_cast<int32_t>(dst->origin.x), static_cast<int32_t>(dst->origin.y),
                                          (dst->texture->dimension == WGPUTextureDimension_3D) ? static_cast<int32_t>(dst->origin.z) : 0};
        region.imageExtent = vk::Extent3D{copySize->width, copySize->height,
                                          (dst->texture->dimension == WGPUTextureDimension_3D) ? copySize->depthOrArrayLayers : 1};

        pe::ImageBarrierInfo barrier{};
        barrier.image = dst->texture->image;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barrier.accessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.layout = vk::ImageLayout::eTransferDstOptimal;
        barrier.baseMipLevel = dst->mipLevel;
        barrier.mipLevels = 1;
        barrier.baseArrayLayer = region.imageSubresource.baseArrayLayer;
        barrier.arrayLayers = region.imageSubresource.layerCount;
        enc->cmd->ImageBarrier(barrier);

        vk::CopyBufferToImageInfo2 copyInfo{};
        copyInfo.srcBuffer = src->buffer->peBuffer->ApiHandle();
        copyInfo.dstImage = dst->texture->image->ApiHandle();
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        enc->cmd->ApiHandle().copyBufferToImage2(copyInfo);
        if (src->buffer)
            enc->retained.usedBuffers.push_back(src->buffer);
    }

    void wgpuCommandEncoderCopyTextureToBuffer(WGPUCommandEncoder enc,
                                               WGPUTexelCopyTextureInfo const *src,
                                               WGPUTexelCopyBufferInfo const *dst,
                                               WGPUExtent3D const *copySize)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyTextureToBuffer"))
            return;
        if (!src || !src->texture || !src->texture->image || src->texture->destroyed)
            return;
        if (!dst || !dst->buffer || !dst->buffer->peBuffer ||
            dst->buffer->internalState == BufferInternalState::Destroyed)
            return;
        if (!copySize)
            return;
        if (!(src->texture->usage & WGPUTextureUsage_CopySrc))
            return;
        if (!(dst->buffer->usage & WGPUBufferUsage_CopyDst))
            return;

        if (dst->layout.bytesPerRow % 256 != 0)
            return;

        if (!ValidateTextureCopyRange(src->texture, src->mipLevel, src->origin, *copySize))
            return;

        vk::ImageAspectFlags vkAspect = AspectForCopy(src->aspect, src->texture->format);

        uint32_t blockW, blockH;
        pwgpu::GetTexelBlockSize(src->texture->format, blockW, blockH);
        uint32_t footprint = pwgpu::TexelBlockCopyFootprint(src->texture->format, src->aspect);

        if (!ValidateBufferCopyLayout(dst->buffer->size, dst->layout.offset,
                                      dst->layout.bytesPerRow, dst->layout.rowsPerImage,
                                      *copySize, footprint, blockW, blockH))
            return;

        vk::BufferImageCopy2 region{};
        region.bufferOffset = dst->layout.offset;
        region.bufferRowLength = (footprint > 0) ? (dst->layout.bytesPerRow / footprint) * blockW : 0;
        region.bufferImageHeight = dst->layout.rowsPerImage * blockH;
        region.imageSubresource.aspectMask = vkAspect;
        region.imageSubresource.mipLevel = src->mipLevel;
        region.imageSubresource.baseArrayLayer = (src->texture->dimension == WGPUTextureDimension_3D) ? 0 : src->origin.z;
        region.imageSubresource.layerCount = (src->texture->dimension == WGPUTextureDimension_3D) ? 1 : copySize->depthOrArrayLayers;
        region.imageOffset = vk::Offset3D{static_cast<int32_t>(src->origin.x), static_cast<int32_t>(src->origin.y),
                                          (src->texture->dimension == WGPUTextureDimension_3D) ? static_cast<int32_t>(src->origin.z) : 0};
        region.imageExtent = vk::Extent3D{copySize->width, copySize->height,
                                          (src->texture->dimension == WGPUTextureDimension_3D) ? copySize->depthOrArrayLayers : 1};

        pe::ImageBarrierInfo imgBarrier{};
        imgBarrier.image = src->texture->image;
        imgBarrier.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        imgBarrier.accessMask = vk::AccessFlagBits2::eTransferRead;
        imgBarrier.layout = vk::ImageLayout::eTransferSrcOptimal;
        imgBarrier.baseMipLevel = src->mipLevel;
        imgBarrier.mipLevels = 1;
        imgBarrier.baseArrayLayer = region.imageSubresource.baseArrayLayer;
        imgBarrier.arrayLayers = region.imageSubresource.layerCount;
        enc->cmd->ImageBarrier(imgBarrier);

        pe::BufferBarrierInfo bufBarrier{};
        bufBarrier.buffer = dst->buffer->peBuffer;
        bufBarrier.stageMask = vk::PipelineStageFlagBits2::eTransfer;
        bufBarrier.accessMask = vk::AccessFlagBits2::eTransferWrite;
        enc->cmd->BufferBarrier(bufBarrier);

        vk::CopyImageToBufferInfo2 copyInfo{};
        copyInfo.srcImage = src->texture->image->ApiHandle();
        copyInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        copyInfo.dstBuffer = dst->buffer->peBuffer->ApiHandle();
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        enc->cmd->ApiHandle().copyImageToBuffer2(copyInfo);
        if (dst->buffer)
            enc->retained.usedBuffers.push_back(dst->buffer);
    }

    void wgpuCommandEncoderCopyTextureToTexture(WGPUCommandEncoder enc,
                                                WGPUTexelCopyTextureInfo const *src,
                                                WGPUTexelCopyTextureInfo const *dst,
                                                WGPUExtent3D const *copySize)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyTextureToTexture"))
            return;
        if (!src || !src->texture || !src->texture->image || src->texture->destroyed)
            return;
        if (!dst || !dst->texture || !dst->texture->image || dst->texture->destroyed)
            return;
        if (!copySize)
            return;
        if (!(src->texture->usage & WGPUTextureUsage_CopySrc))
            return;
        if (!(dst->texture->usage & WGPUTextureUsage_CopyDst))
            return;

        if (!ValidateTextureCopyRange(src->texture, src->mipLevel, src->origin, *copySize))
            return;
        if (!ValidateTextureCopyRange(dst->texture, dst->mipLevel, dst->origin, *copySize))
            return;

        vk::ImageAspectFlags srcAspect = AspectForCopy(src->aspect, src->texture->format);
        vk::ImageAspectFlags dstAspect = AspectForCopy(dst->aspect, dst->texture->format);

        bool src3D = (src->texture->dimension == WGPUTextureDimension_3D);
        bool dst3D = (dst->texture->dimension == WGPUTextureDimension_3D);

        vk::ImageCopy2 region{};
        region.srcSubresource.aspectMask = srcAspect;
        region.srcSubresource.mipLevel = src->mipLevel;
        region.srcSubresource.baseArrayLayer = src3D ? 0 : src->origin.z;
        region.srcSubresource.layerCount = src3D ? 1 : copySize->depthOrArrayLayers;
        region.srcOffset = vk::Offset3D{static_cast<int32_t>(src->origin.x), static_cast<int32_t>(src->origin.y),
                                        src3D ? static_cast<int32_t>(src->origin.z) : 0};
        region.dstSubresource.aspectMask = dstAspect;
        region.dstSubresource.mipLevel = dst->mipLevel;
        region.dstSubresource.baseArrayLayer = dst3D ? 0 : dst->origin.z;
        region.dstSubresource.layerCount = dst3D ? 1 : copySize->depthOrArrayLayers;
        region.dstOffset = vk::Offset3D{static_cast<int32_t>(dst->origin.x), static_cast<int32_t>(dst->origin.y),
                                        dst3D ? static_cast<int32_t>(dst->origin.z) : 0};
        region.extent = vk::Extent3D{copySize->width, copySize->height,
                                     (src3D || dst3D) ? copySize->depthOrArrayLayers : 1};

        std::vector<pe::ImageBarrierInfo> barriers(2);
        barriers[0].image = src->texture->image;
        barriers[0].stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barriers[0].accessMask = vk::AccessFlagBits2::eTransferRead;
        barriers[0].layout = vk::ImageLayout::eTransferSrcOptimal;
        barriers[0].baseMipLevel = src->mipLevel;
        barriers[0].mipLevels = 1;
        barriers[0].baseArrayLayer = region.srcSubresource.baseArrayLayer;
        barriers[0].arrayLayers = region.srcSubresource.layerCount;

        barriers[1].image = dst->texture->image;
        barriers[1].stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barriers[1].accessMask = vk::AccessFlagBits2::eTransferWrite;
        barriers[1].layout = vk::ImageLayout::eTransferDstOptimal;
        barriers[1].baseMipLevel = dst->mipLevel;
        barriers[1].mipLevels = 1;
        barriers[1].baseArrayLayer = region.dstSubresource.baseArrayLayer;
        barriers[1].arrayLayers = region.dstSubresource.layerCount;

        enc->cmd->ImageBarriers(barriers);

        vk::CopyImageInfo2 copyInfo{};
        copyInfo.srcImage = src->texture->image->ApiHandle();
        copyInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        copyInfo.dstImage = dst->texture->image->ApiHandle();
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        enc->cmd->ApiHandle().copyImage2(copyInfo);
    }

    void wgpuCommandEncoderResolveQuerySet(WGPUCommandEncoder enc,
                                           WGPUQuerySet querySet,
                                           uint32_t firstQuery,
                                           uint32_t queryCount,
                                           WGPUBuffer dst,
                                           uint64_t dstOffset)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderResolveQuerySet"))
            return;
        if (!querySet || !querySet->queryPool)
            return;
        if (!dst || !dst->peBuffer ||
            dst->internalState == BufferInternalState::Destroyed)
            return;
        if (!(dst->usage & WGPUBufferUsage_QueryResolve))
            return;
        if (firstQuery + queryCount > querySet->count)
            return;
        if (dstOffset % 256 != 0)
            return;
        if (dstOffset + queryCount * 8 > dst->size)
            return;

        wgpuQuerySetAddRef(querySet);
        enc->retained.querySets.push_back(querySet);

        enc->cmd->ApiHandle().copyQueryPoolResults(
            querySet->queryPool, firstQuery, queryCount,
            dst->peBuffer->ApiHandle(), dstOffset,
            sizeof(uint64_t), vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        enc->cmd->ApiHandle().resetQueryPool(querySet->queryPool, firstQuery, queryCount);
        enc->retained.usedBuffers.push_back(dst);
    }

    void wgpuCommandEncoderWriteTimestamp(WGPUCommandEncoder enc, WGPUQuerySet querySet, uint32_t queryIndex)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderWriteTimestamp"))
            return;
        if (!querySet || !querySet->queryPool)
            return;
        if (queryIndex >= querySet->count)
            return;

        wgpuQuerySetAddRef(querySet);
        enc->retained.querySets.push_back(querySet);

        enc->cmd->ApiHandle().writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,
                                              querySet->queryPool, queryIndex);
    }

    void wgpuCommandEncoderInsertDebugMarker(WGPUCommandEncoder enc, WGPUStringView markerLabel)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderInsertDebugMarker"))
            return;
        enc->cmd->InsertDebugLabel(pwgpu::ToString(markerLabel));
    }

    void wgpuCommandEncoderPushDebugGroup(WGPUCommandEncoder enc, WGPUStringView groupLabel)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderPushDebugGroup"))
            return;
        enc->cmd->BeginDebugRegion(pwgpu::ToString(groupLabel));
    }

    void wgpuCommandEncoderPopDebugGroup(WGPUCommandEncoder enc)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderPopDebugGroup"))
            return;
        enc->cmd->EndDebugRegion();
    }

    void wgpuCommandEncoderSetLabel(WGPUCommandEncoder enc, WGPUStringView label)
    {
        if (enc)
            enc->label = pwgpu::ToString(label);
    }

    void wgpuCommandBufferAddRef(WGPUCommandBuffer cb)
    {
        if (cb)
            cb->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuCommandBufferRelease(WGPUCommandBuffer cb)
    {
        if (!cb)
            return;
        if (cb->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            cb->retained.ReleaseAll();
            if (cb->cmd && !cb->submitted)
                cb->cmd->Return();
            delete cb;
        }
    }

    void wgpuCommandBufferSetLabel(WGPUCommandBuffer cb, WGPUStringView label)
    {
        if (cb)
            cb->label = pwgpu::ToString(label);
    }

} // extern "C"
