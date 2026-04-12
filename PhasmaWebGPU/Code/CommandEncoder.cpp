#include "CommandEncoder.h"
#include "RenderPass.h"
#include "ComputePass.h"
#include "Buffer.h"
#include "Texture.h"
#include "QuerySet.h"
#include "Utils.h"

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
            delete enc;
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
        if (descriptor && descriptor->label.data)
            rpe->label = pwgpu::ToString(descriptor->label);
        enc->hasOpenPass = true;
        return rpe;
    }

    WGPUComputePassEncoder wgpuCommandEncoderBeginComputePass(WGPUCommandEncoder enc,
                                                              WGPUComputePassDescriptor const *descriptor)
    {
        (void)descriptor;
        if (!enc || enc->finished || enc->hasOpenPass)
            return nullptr;
        auto *cpe = new WGPUComputePassEncoderImpl();
        cpe->cmd = enc->cmd;
        cpe->parent = enc;
        if (descriptor && descriptor->label.data)
            cpe->label = pwgpu::ToString(descriptor->label);
        enc->hasOpenPass = true;
        return cpe;
    }

    WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder enc,
                                               WGPUCommandBufferDescriptor const *descriptor)
    {
        if (!enc || enc->finished || enc->hasOpenPass)
            return nullptr;
        enc->finished = true;
        auto *cb = new WGPUCommandBufferImpl();
        cb->cmd = enc->cmd;
        enc->cmd = nullptr;
        if (descriptor && descriptor->label.data)
            cb->label = pwgpu::ToString(descriptor->label);
        return cb;
    }

    void wgpuCommandEncoderCopyBufferToBuffer(WGPUCommandEncoder enc,
                                              WGPUBuffer src, uint64_t srcOffset,
                                              WGPUBuffer dst, uint64_t dstOffset,
                                              uint64_t size)
    {
        (void)enc;
        (void)src;
        (void)srcOffset;
        (void)dst;
        (void)dstOffset;
        (void)size;
    }

    void wgpuCommandEncoderCopyBufferToTexture(WGPUCommandEncoder enc,
                                               WGPUTexelCopyBufferInfo const *src,
                                               WGPUTexelCopyTextureInfo const *dst,
                                               WGPUExtent3D const *copySize)
    {
        (void)enc;
        (void)src;
        (void)dst;
        (void)copySize;
    }

    void wgpuCommandEncoderCopyTextureToBuffer(WGPUCommandEncoder enc,
                                               WGPUTexelCopyTextureInfo const *src,
                                               WGPUTexelCopyBufferInfo const *dst,
                                               WGPUExtent3D const *copySize)
    {
        (void)enc;
        (void)src;
        (void)dst;
        (void)copySize;
    }

    void wgpuCommandEncoderCopyTextureToTexture(WGPUCommandEncoder enc,
                                                WGPUTexelCopyTextureInfo const *src,
                                                WGPUTexelCopyTextureInfo const *dst,
                                                WGPUExtent3D const *copySize)
    {
        (void)enc;
        (void)src;
        (void)dst;
        (void)copySize;
    }

    void wgpuCommandEncoderClearBuffer(WGPUCommandEncoder enc,
                                       WGPUBuffer buffer,
                                       uint64_t offset,
                                       uint64_t size)
    {
        (void)enc;
        (void)buffer;
        (void)offset;
        (void)size;
    }

    void wgpuCommandEncoderResolveQuerySet(WGPUCommandEncoder enc,
                                           WGPUQuerySet querySet,
                                           uint32_t firstQuery,
                                           uint32_t queryCount,
                                           WGPUBuffer dst,
                                           uint64_t dstOffset)
    {
        (void)enc;
        (void)querySet;
        (void)firstQuery;
        (void)queryCount;
        (void)dst;
        (void)dstOffset;
    }

    void wgpuCommandEncoderWriteTimestamp(WGPUCommandEncoder enc, WGPUQuerySet querySet, uint32_t queryIndex)
    {
        (void)enc;
        (void)querySet;
        (void)queryIndex;
    }

    void wgpuCommandEncoderInsertDebugMarker(WGPUCommandEncoder enc, WGPUStringView markerLabel)
    {
        (void)enc;
        (void)markerLabel;
    }

    void wgpuCommandEncoderPushDebugGroup(WGPUCommandEncoder enc, WGPUStringView groupLabel)
    {
        (void)enc;
        (void)groupLabel;
    }

    void wgpuCommandEncoderPopDebugGroup(WGPUCommandEncoder enc)
    {
        (void)enc;
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
            delete cb;
    }

    void wgpuCommandBufferSetLabel(WGPUCommandBuffer cb, WGPUStringView label)
    {
        if (cb)
            cb->label = pwgpu::ToString(label);
    }

} // extern "C"
