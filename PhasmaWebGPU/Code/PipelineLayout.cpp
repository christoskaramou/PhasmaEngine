#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Device.h"
#include "Utils.h"
#include "API/RHI.h"

extern "C"
{

    void wgpuPipelineLayoutAddRef(WGPUPipelineLayout pl)
    {
        if (pl)
            pl->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuPipelineLayoutRelease(WGPUPipelineLayout pl)
    {
        if (!pl)
            return;
        if (pl->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            for (auto *bgl : pl->bindGroupLayouts)
            {
                if (bgl)
                    wgpuBindGroupLayoutRelease(bgl);
            }
            if (pl->device && pl->device->rhi)
            {
                auto vkDev = pl->device->rhi->GetDevice();
                if (pl->vkLayout != VK_NULL_HANDLE)
                    vkDev.destroyPipelineLayout(pl->vkLayout);
                for (auto emptyLayout : pl->ownedEmptySetLayouts)
                    vkDev.destroyDescriptorSetLayout(emptyLayout);
            }
            WGPUDeviceImpl *dev = pl->device;
            delete pl;
            if (dev)
                wgpuDeviceRelease(dev);
        }
    }

    void wgpuPipelineLayoutSetLabel(WGPUPipelineLayout pl, WGPUStringView label)
    {
        if (pl)
            pl->label = pwgpu::ToString(label);
    }

} // extern "C"
