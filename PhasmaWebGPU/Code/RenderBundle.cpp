#include "RenderBundle.h"
#include "RenderPipeline.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "Utils.h"

extern "C"
{

    void wgpuRenderBundleAddRef(WGPURenderBundle rb)
    {
        if (rb)
            rb->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuRenderBundleRelease(WGPURenderBundle rb)
    {
        if (!rb)
            return;
        if (rb->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete rb;
    }

    void wgpuRenderBundleSetLabel(WGPURenderBundle rb, WGPUStringView label)
    {
        if (rb)
            rb->label = pwgpu::ToString(label);
    }

    void wgpuRenderBundleEncoderAddRef(WGPURenderBundleEncoder rbe)
    {
        if (rbe)
            rbe->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuRenderBundleEncoderRelease(WGPURenderBundleEncoder rbe)
    {
        if (!rbe)
            return;
        if (rbe->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete rbe;
    }

    void wgpuRenderBundleEncoderSetPipeline(WGPURenderBundleEncoder rbe, WGPURenderPipeline pipeline)
    {
        (void)rbe;
        (void)pipeline;
    }

    void wgpuRenderBundleEncoderSetBindGroup(WGPURenderBundleEncoder rbe, uint32_t groupIndex,
                                             WGPUBindGroup group,
                                             size_t dynamicOffsetCount, uint32_t const *dynamicOffsets)
    {
        (void)rbe;
        (void)groupIndex;
        (void)group;
        (void)dynamicOffsetCount;
        (void)dynamicOffsets;
    }

    void wgpuRenderBundleEncoderSetVertexBuffer(WGPURenderBundleEncoder rbe, uint32_t slot,
                                                WGPUBuffer buffer, uint64_t offset, uint64_t size)
    {
        (void)rbe;
        (void)slot;
        (void)buffer;
        (void)offset;
        (void)size;
    }

    void wgpuRenderBundleEncoderSetIndexBuffer(WGPURenderBundleEncoder rbe, WGPUBuffer buffer,
                                               WGPUIndexFormat format, uint64_t offset, uint64_t size)
    {
        (void)rbe;
        (void)buffer;
        (void)format;
        (void)offset;
        (void)size;
    }

    void wgpuRenderBundleEncoderDraw(WGPURenderBundleEncoder rbe, uint32_t vertexCount,
                                     uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        (void)rbe;
        (void)vertexCount;
        (void)instanceCount;
        (void)firstVertex;
        (void)firstInstance;
    }

    void wgpuRenderBundleEncoderDrawIndexed(WGPURenderBundleEncoder rbe, uint32_t indexCount,
                                            uint32_t instanceCount, uint32_t firstIndex,
                                            int32_t baseVertex, uint32_t firstInstance)
    {
        (void)rbe;
        (void)indexCount;
        (void)instanceCount;
        (void)firstIndex;
        (void)baseVertex;
        (void)firstInstance;
    }

    void wgpuRenderBundleEncoderDrawIndirect(WGPURenderBundleEncoder rbe, WGPUBuffer buffer, uint64_t offset)
    {
        (void)rbe;
        (void)buffer;
        (void)offset;
    }

    void wgpuRenderBundleEncoderDrawIndexedIndirect(WGPURenderBundleEncoder rbe, WGPUBuffer buffer, uint64_t offset)
    {
        (void)rbe;
        (void)buffer;
        (void)offset;
    }

    WGPURenderBundle wgpuRenderBundleEncoderFinish(WGPURenderBundleEncoder rbe,
                                                   WGPURenderBundleDescriptor const *descriptor)
    {
        if (!rbe || rbe->finished)
            return nullptr;
        rbe->finished = true;
        auto *rb = new WGPURenderBundleImpl();
        rb->cmd = rbe->cmd;
        rbe->cmd = nullptr;
        if (descriptor && descriptor->label.data)
            rb->label = pwgpu::ToString(descriptor->label);
        return rb;
    }

    void wgpuRenderBundleEncoderInsertDebugMarker(WGPURenderBundleEncoder rbe, WGPUStringView label)
    {
        (void)rbe;
        (void)label;
    }

    void wgpuRenderBundleEncoderPushDebugGroup(WGPURenderBundleEncoder rbe, WGPUStringView groupLabel)
    {
        (void)rbe;
        (void)groupLabel;
    }

    void wgpuRenderBundleEncoderPopDebugGroup(WGPURenderBundleEncoder rbe)
    {
        (void)rbe;
    }

    void wgpuRenderBundleEncoderSetLabel(WGPURenderBundleEncoder rbe, WGPUStringView label)
    {
        if (rbe)
            rbe->label = pwgpu::ToString(label);
    }

} // extern "C"
