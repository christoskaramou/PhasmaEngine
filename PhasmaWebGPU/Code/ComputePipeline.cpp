#include "ComputePipeline.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Device.h"
#include "Utils.h"
#include "API/RHI.h"

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
            if (cp->device && cp->device->rhi && cp->backendPipeline != 0)
            {
                pe::VulkanRhi::Device().destroyPipeline(
                    vk::Pipeline{PeFromBackendHandle<VkPipeline>(cp->backendPipeline)});
            }
            if (cp->layout)
                wgpuPipelineLayoutRelease(cp->layout);
            WGPUDeviceImpl *dev = cp->device;
            delete cp;
            if (dev)
                wgpuDeviceRelease(dev);
        }
    }

    WGPUBindGroupLayout wgpuComputePipelineGetBindGroupLayout(WGPUComputePipeline cp, uint32_t groupIndex)
    {
        if (!cp)
            return nullptr;
        WGPUDeviceImpl *device = cp->device;
        if (device && groupIndex >= device->limits.maxBindGroups)
        {
            device->reportError(WGPUErrorType_Validation,
                                pwgpu::ToStringView("getBindGroupLayout: index out of bounds"));
            return nullptr;
        }
        if (!cp->layout || groupIndex >= cp->layout->bindGroupLayouts.size())
            return nullptr;
        WGPUBindGroupLayout bgl = cp->layout->bindGroupLayouts[groupIndex];
        if (bgl)
            wgpuBindGroupLayoutAddRef(bgl);
        return bgl;
    }

    void wgpuComputePipelineSetLabel(WGPUComputePipeline cp, WGPUStringView label)
    {
        if (cp)
            cp->label = pwgpu::ToString(label);
    }

} // extern "C"
