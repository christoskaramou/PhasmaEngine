#include "ComputePipeline.h"
#include "BindGroup.h"
#include "Utils.h"

extern "C"
{

    void wgpuComputePipelineAddRef(WGPUComputePipeline cp)
    {
        if (cp)
            cp->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuComputePipelineRelease(WGPUComputePipeline cp)
    {
        if (!cp)
            return;
        if (cp->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            for (auto *bgl : cp->bindGroupLayouts)
                wgpuBindGroupLayoutRelease(bgl);
            if (cp->pipeline)
                pe::Pipeline::Destroy(cp->pipeline);
            delete cp->passInfo;
            delete cp;
        }
    }

    WGPUBindGroupLayout wgpuComputePipelineGetBindGroupLayout(WGPUComputePipeline cp, uint32_t groupIndex)
    {
        if (!cp || groupIndex >= cp->bindGroupLayouts.size())
            return nullptr;
        WGPUBindGroupLayout bgl = cp->bindGroupLayouts[groupIndex];
        wgpuBindGroupLayoutAddRef(bgl);
        return bgl;
    }

    void wgpuComputePipelineSetLabel(WGPUComputePipeline cp, WGPUStringView label)
    {
        if (cp)
            cp->label = pwgpu::ToString(label);
    }

} // extern "C"
