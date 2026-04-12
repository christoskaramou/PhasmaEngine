#include "BindGroup.h"
#include "Device.h"
#include "Utils.h"

extern "C"
{

    void wgpuBindGroupLayoutAddRef(WGPUBindGroupLayout bgl)
    {
        if (bgl)
            bgl->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuBindGroupLayoutRelease(WGPUBindGroupLayout bgl)
    {
        if (!bgl)
            return;
        if (bgl->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            if (bgl->layout)
                pe::DescriptorLayout::Destroy(bgl->layout);
            WGPUDeviceImpl *dev = bgl->device;
            delete bgl;
            if (dev)
                wgpuDeviceRelease(dev);
        }
    }

    void wgpuBindGroupLayoutSetLabel(WGPUBindGroupLayout bgl, WGPUStringView label)
    {
        if (bgl)
            bgl->label = pwgpu::ToString(label);
    }

    void wgpuBindGroupAddRef(WGPUBindGroup bg)
    {
        if (bg)
            bg->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuBindGroupRelease(WGPUBindGroup bg)
    {
        if (!bg)
            return;
        if (bg->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            if (bg->layout)
                wgpuBindGroupLayoutRelease(bg->layout);
            if (bg->descriptor)
                pe::Descriptor::Destroy(bg->descriptor);
            WGPUDeviceImpl *dev = bg->device;
            delete bg;
            if (dev)
                wgpuDeviceRelease(dev);
        }
    }

    void wgpuBindGroupSetLabel(WGPUBindGroup bg, WGPUStringView label)
    {
        if (bg)
            bg->label = pwgpu::ToString(label);
    }

} // extern "C"
