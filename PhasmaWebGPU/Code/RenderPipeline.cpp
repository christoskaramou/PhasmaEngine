#include "RenderPipeline.h"
#include "PipelineCreation.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Device.h"
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
            if (rp->device && rp->device->rhi && rp->backendPipeline != 0)
                pwgpu::DestroyWebGPUPipelineBackend(rp->device, rp->backendPipeline);
            if (rp->layout)
                wgpuPipelineLayoutRelease(rp->layout);
            WGPUDeviceImpl *dev = rp->device;
            delete rp;
            if (dev)
                wgpuDeviceRelease(dev);
        }
    }

    WGPUBindGroupLayout wgpuRenderPipelineGetBindGroupLayout(WGPURenderPipeline rp, uint32_t groupIndex)
    {
        if (!rp)
            return nullptr;
        WGPUDeviceImpl *device = rp->device;
        if (device && groupIndex >= device->limits.maxBindGroups)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("getBindGroupLayout: index out of bounds"));
            return nullptr;
        }
        if (!rp->layout || groupIndex >= rp->layout->bindGroupLayouts.size())
            return nullptr;
        WGPUBindGroupLayout bgl = rp->layout->bindGroupLayouts[groupIndex];
        if (bgl)
            wgpuBindGroupLayoutAddRef(bgl);
        return bgl;
    }

    void wgpuRenderPipelineSetLabel(WGPURenderPipeline rp, WGPUStringView label)
    {
        if (rp)
            rp->label = pwgpu::ToString(label);
    }

} // extern "C"
