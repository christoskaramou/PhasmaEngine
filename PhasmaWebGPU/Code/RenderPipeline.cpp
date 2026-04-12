#include "RenderPipeline.h"
#include "BindGroup.h"
#include "Utils.h"

extern "C"
{

    void wgpuRenderPipelineAddRef(WGPURenderPipeline rp)
    {
        if (rp)
            rp->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuRenderPipelineRelease(WGPURenderPipeline rp)
    {
        if (!rp)
            return;
        if (rp->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            for (auto *bgl : rp->bindGroupLayouts)
                wgpuBindGroupLayoutRelease(bgl);
            if (rp->pipeline)
                pe::Pipeline::Destroy(rp->pipeline);
            delete rp->passInfo;
            delete rp;
        }
    }

    WGPUBindGroupLayout wgpuRenderPipelineGetBindGroupLayout(WGPURenderPipeline rp, uint32_t groupIndex)
    {
        if (!rp || groupIndex >= rp->bindGroupLayouts.size())
            return nullptr;
        WGPUBindGroupLayout bgl = rp->bindGroupLayouts[groupIndex];
        wgpuBindGroupLayoutAddRef(bgl);
        return bgl;
    }

    void wgpuRenderPipelineSetLabel(WGPURenderPipeline rp, WGPUStringView label)
    {
        if (rp)
            rp->label = pwgpu::ToString(label);
    }

} // extern "C"
