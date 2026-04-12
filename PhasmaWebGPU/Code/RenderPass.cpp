#include "RenderPass.h"
#include "CommandEncoder.h"
#include "RenderPipeline.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "RenderBundle.h"
#include "QuerySet.h"
#include "Utils.h"

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
            delete rpe;
    }

    void wgpuRenderPassEncoderSetPipeline(WGPURenderPassEncoder rpe, WGPURenderPipeline pipeline)
    {
        (void)rpe;
        (void)pipeline;
    }

    void wgpuRenderPassEncoderSetBindGroup(WGPURenderPassEncoder rpe, uint32_t groupIndex,
                                           WGPUBindGroup group,
                                           size_t dynamicOffsetCount, uint32_t const *dynamicOffsets)
    {
        (void)rpe;
        (void)groupIndex;
        (void)group;
        (void)dynamicOffsetCount;
        (void)dynamicOffsets;
    }

    void wgpuRenderPassEncoderSetVertexBuffer(WGPURenderPassEncoder rpe, uint32_t slot,
                                              WGPUBuffer buffer, uint64_t offset, uint64_t size)
    {
        (void)rpe;
        (void)slot;
        (void)buffer;
        (void)offset;
        (void)size;
    }

    void wgpuRenderPassEncoderSetIndexBuffer(WGPURenderPassEncoder rpe, WGPUBuffer buffer,
                                             WGPUIndexFormat format, uint64_t offset, uint64_t size)
    {
        (void)rpe;
        (void)buffer;
        (void)format;
        (void)offset;
        (void)size;
    }

    void wgpuRenderPassEncoderDraw(WGPURenderPassEncoder rpe, uint32_t vertexCount,
                                   uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        (void)rpe;
        (void)vertexCount;
        (void)instanceCount;
        (void)firstVertex;
        (void)firstInstance;
    }

    void wgpuRenderPassEncoderDrawIndexed(WGPURenderPassEncoder rpe, uint32_t indexCount,
                                          uint32_t instanceCount, uint32_t firstIndex,
                                          int32_t baseVertex, uint32_t firstInstance)
    {
        (void)rpe;
        (void)indexCount;
        (void)instanceCount;
        (void)firstIndex;
        (void)baseVertex;
        (void)firstInstance;
    }

    void wgpuRenderPassEncoderDrawIndirect(WGPURenderPassEncoder rpe, WGPUBuffer buffer, uint64_t offset)
    {
        (void)rpe;
        (void)buffer;
        (void)offset;
    }

    void wgpuRenderPassEncoderDrawIndexedIndirect(WGPURenderPassEncoder rpe, WGPUBuffer buffer, uint64_t offset)
    {
        (void)rpe;
        (void)buffer;
        (void)offset;
    }

    void wgpuRenderPassEncoderSetViewport(WGPURenderPassEncoder rpe,
                                          float x, float y, float width, float height,
                                          float minDepth, float maxDepth)
    {
        (void)rpe;
        (void)x;
        (void)y;
        (void)width;
        (void)height;
        (void)minDepth;
        (void)maxDepth;
    }

    void wgpuRenderPassEncoderSetScissorRect(WGPURenderPassEncoder rpe,
                                             uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        (void)rpe;
        (void)x;
        (void)y;
        (void)width;
        (void)height;
    }

    void wgpuRenderPassEncoderSetBlendConstant(WGPURenderPassEncoder rpe, WGPUColor const *color)
    {
        (void)rpe;
        (void)color;
    }

    void wgpuRenderPassEncoderSetStencilReference(WGPURenderPassEncoder rpe, uint32_t reference)
    {
        (void)rpe;
        (void)reference;
    }

    void wgpuRenderPassEncoderBeginOcclusionQuery(WGPURenderPassEncoder rpe, uint32_t queryIndex)
    {
        (void)rpe;
        (void)queryIndex;
    }

    void wgpuRenderPassEncoderEndOcclusionQuery(WGPURenderPassEncoder rpe)
    {
        (void)rpe;
    }

    void wgpuRenderPassEncoderExecuteBundles(WGPURenderPassEncoder rpe,
                                             size_t bundleCount, WGPURenderBundle const *bundles)
    {
        (void)rpe;
        (void)bundleCount;
        (void)bundles;
    }

    void wgpuRenderPassEncoderInsertDebugMarker(WGPURenderPassEncoder rpe, WGPUStringView label)
    {
        (void)rpe;
        (void)label;
    }

    void wgpuRenderPassEncoderPushDebugGroup(WGPURenderPassEncoder rpe, WGPUStringView groupLabel)
    {
        (void)rpe;
        (void)groupLabel;
    }

    void wgpuRenderPassEncoderPopDebugGroup(WGPURenderPassEncoder rpe)
    {
        (void)rpe;
    }

    void wgpuRenderPassEncoderEnd(WGPURenderPassEncoder rpe)
    {
        // Idempotent: a second End() must not double-clear parent->hasOpenPass.
        if (!rpe || rpe->ended)
            return;
        rpe->ended = true;
        if (rpe->parent)
            rpe->parent->hasOpenPass = false;
    }

    void wgpuRenderPassEncoderSetLabel(WGPURenderPassEncoder rpe, WGPUStringView label)
    {
        if (rpe)
            rpe->label = pwgpu::ToString(label);
    }

} // extern "C"
