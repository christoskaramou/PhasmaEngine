#include "PipelineLayout.h"
#include "BindGroup.h"
#include "Utils.h"

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
                wgpuBindGroupLayoutRelease(bgl);
            if (pl->vkLayout != VK_NULL_HANDLE)
                pe::RHI::Get()->GetDevice().destroyPipelineLayout(pl->vkLayout);
            delete pl;
        }
    }

    void wgpuPipelineLayoutSetLabel(WGPUPipelineLayout pl, WGPUStringView label)
    {
        if (pl)
            pl->label = pwgpu::ToString(label);
    }

} // extern "C"
