#include "ComputePass.h"
#include "CommandEncoder.h"
#include "ComputePipeline.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "QuerySet.h"
#include "Utils.h"

extern "C"
{

    void wgpuComputePassEncoderAddRef(WGPUComputePassEncoder cpe)
    {
        if (cpe)
            cpe->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuComputePassEncoderRelease(WGPUComputePassEncoder cpe)
    {
        if (!cpe)
            return;
        if (cpe->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete cpe;
    }

    void wgpuComputePassEncoderSetPipeline(WGPUComputePassEncoder cpe, WGPUComputePipeline pipeline)
    {
        (void)cpe;
        (void)pipeline;
    }

    void wgpuComputePassEncoderSetBindGroup(WGPUComputePassEncoder cpe, uint32_t groupIndex,
                                            WGPUBindGroup group,
                                            size_t dynamicOffsetCount, uint32_t const *dynamicOffsets)
    {
        (void)cpe;
        (void)groupIndex;
        (void)group;
        (void)dynamicOffsetCount;
        (void)dynamicOffsets;
    }

    void wgpuComputePassEncoderDispatchWorkgroups(WGPUComputePassEncoder cpe,
                                                  uint32_t x, uint32_t y, uint32_t z)
    {
        (void)cpe;
        (void)x;
        (void)y;
        (void)z;
    }

    void wgpuComputePassEncoderDispatchWorkgroupsIndirect(WGPUComputePassEncoder cpe,
                                                          WGPUBuffer buffer, uint64_t offset)
    {
        (void)cpe;
        (void)buffer;
        (void)offset;
    }

    void wgpuComputePassEncoderEnd(WGPUComputePassEncoder cpe)
    {
        // Idempotent: a second End() must not double-clear parent->hasOpenPass.
        if (!cpe || cpe->ended)
            return;
        cpe->ended = true;
        if (cpe->parent)
            cpe->parent->hasOpenPass = false;
    }

    void wgpuComputePassEncoderInsertDebugMarker(WGPUComputePassEncoder cpe, WGPUStringView label)
    {
        (void)cpe;
        (void)label;
    }

    void wgpuComputePassEncoderPushDebugGroup(WGPUComputePassEncoder cpe, WGPUStringView groupLabel)
    {
        (void)cpe;
        (void)groupLabel;
    }

    void wgpuComputePassEncoderPopDebugGroup(WGPUComputePassEncoder cpe)
    {
        (void)cpe;
    }

    void wgpuComputePassEncoderSetLabel(WGPUComputePassEncoder cpe, WGPUStringView label)
    {
        if (cpe)
            cpe->label = pwgpu::ToString(label);
    }

} // extern "C"
