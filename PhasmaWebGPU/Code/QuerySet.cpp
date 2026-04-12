#include "QuerySet.h"
#include "Utils.h"

extern "C"
{

    void wgpuQuerySetAddRef(WGPUQuerySet qs)
    {
        if (qs)
            qs->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuQuerySetRelease(WGPUQuerySet qs)
    {
        if (!qs)
            return;
        if (qs->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            if (qs->queryPool != VK_NULL_HANDLE)
                pe::RHI::Get()->GetDevice().destroyQueryPool(qs->queryPool);
            delete qs;
        }
    }

    void wgpuQuerySetDestroy(WGPUQuerySet qs)
    {
        if (!qs || qs->destroyed)
            return;
        qs->destroyed = true;
        if (qs->queryPool != VK_NULL_HANDLE)
        {
            pe::RHI::Get()->GetDevice().destroyQueryPool(qs->queryPool);
            qs->queryPool = VK_NULL_HANDLE;
        }
    }

    uint32_t wgpuQuerySetGetCount(WGPUQuerySet qs)
    {
        return qs ? qs->count : 0;
    }

    WGPUQueryType wgpuQuerySetGetType(WGPUQuerySet qs)
    {
        return qs ? qs->type : WGPUQueryType_Occlusion;
    }

    void wgpuQuerySetSetLabel(WGPUQuerySet qs, WGPUStringView label)
    {
        if (qs)
            qs->label = pwgpu::ToString(label);
    }

} // extern "C"
