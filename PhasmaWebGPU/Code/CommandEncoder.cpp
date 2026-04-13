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

void RetainedResources::MergeFrom(RetainedResources &other)
{
    renderPipelines.insert(renderPipelines.end(), other.renderPipelines.begin(), other.renderPipelines.end());
    computePipelines.insert(computePipelines.end(), other.computePipelines.begin(), other.computePipelines.end());
    bindGroups.insert(bindGroups.end(), other.bindGroups.begin(), other.bindGroups.end());
    querySets.insert(querySets.end(), other.querySets.begin(), other.querySets.end());
    other.renderPipelines.clear();
    other.computePipelines.clear();
    other.bindGroups.clear();
    other.querySets.clear();
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
    renderPipelines.clear();
    computePipelines.clear();
    bindGroups.clear();
    querySets.clear();
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
        (void)descriptor;
        if (!enc || enc->finished || enc->hasOpenPass)
            return nullptr;
        auto *rpe = new WGPURenderPassEncoderImpl();
        rpe->cmd = enc->cmd;
        rpe->parent = enc;
        rpe->device = enc->device;
        if (descriptor && descriptor->label.data)
            rpe->label = pwgpu::ToString(descriptor->label);
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

    // ---- §12.1.1 finish() → GPUCommandBuffer ----

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

        cb->retained.MergeFrom(enc->retained);

        if (descriptor && descriptor->label.data)
            cb->label = pwgpu::ToString(descriptor->label);
        return cb;
    }

    // ---- §11.1 / §13.4 copyBufferToBuffer ----

    void wgpuCommandEncoderCopyBufferToBuffer(WGPUCommandEncoder enc,
                                              WGPUBuffer src, uint64_t srcOffset,
                                              WGPUBuffer dst, uint64_t dstOffset,
                                              uint64_t size)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyBufferToBuffer"))
            return;
        if (!src || !src->peBuffer || src->destroyed ||
            !dst || !dst->peBuffer || dst->destroyed)
            return;

        if (src == dst)
            return;
        if (!(src->usage & WGPUBufferUsage_CopySrc))
            return;
        if (!(dst->usage & WGPUBufferUsage_CopyDst))
            return;

        if (srcOffset > src->size)
            return;
        if (dstOffset > dst->size)
            return;

        uint64_t copySize = size;
        if (copySize == WGPU_WHOLE_SIZE)
            copySize = src->size - srcOffset;

        if (srcOffset + copySize > src->size)
            return;
        if (dstOffset + copySize > dst->size)
            return;
        if (copySize % 4 != 0)
            return;
        if (srcOffset % 4 != 0)
            return;
        if (dstOffset % 4 != 0)
            return;

        enc->cmd->CopyBuffer(src->peBuffer, dst->peBuffer, static_cast<size_t>(copySize),
                             static_cast<size_t>(srcOffset), static_cast<size_t>(dstOffset));
    }

    // ---- §11.1 / §13.4 clearBuffer ----

    void wgpuCommandEncoderClearBuffer(WGPUCommandEncoder enc,
                                       WGPUBuffer buffer,
                                       uint64_t offset,
                                       uint64_t size)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderClearBuffer"))
            return;
        if (!buffer || !buffer->peBuffer || buffer->destroyed)
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
    }

    // ---- §11.2 / §13.5 copyBufferToTexture ----

    void wgpuCommandEncoderCopyBufferToTexture(WGPUCommandEncoder enc,
                                               WGPUTexelCopyBufferInfo const *src,
                                               WGPUTexelCopyTextureInfo const *dst,
                                               WGPUExtent3D const *copySize)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyBufferToTexture"))
            return;
        if (!src || !src->buffer || !src->buffer->peBuffer || src->buffer->destroyed)
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
    }

    // ---- §11.2 / §13.5 copyTextureToBuffer ----

    void wgpuCommandEncoderCopyTextureToBuffer(WGPUCommandEncoder enc,
                                               WGPUTexelCopyTextureInfo const *src,
                                               WGPUTexelCopyBufferInfo const *dst,
                                               WGPUExtent3D const *copySize)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyTextureToBuffer"))
            return;
        if (!src || !src->texture || !src->texture->image || src->texture->destroyed)
            return;
        if (!dst || !dst->buffer || !dst->buffer->peBuffer || dst->buffer->destroyed)
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
    }

    // ---- §11.2 / §13.5 copyTextureToTexture ----

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

    // ---- §13.6 resolveQuerySet ----

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
        if (!dst || !dst->peBuffer || dst->destroyed)
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
    }

    // ---- §13.4 writeTimestamp (extension) ----

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

    // ---- §15 Debug markers ----

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

    // ==== WGPUCommandBuffer ====

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
            // If the command buffer was never submitted, return the pe::CommandBuffer
            // to the pool so we don't leak it.
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
