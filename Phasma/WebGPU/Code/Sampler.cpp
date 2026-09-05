#include "Sampler.h"
#include "Device.h"
#include "Utils.h"

extern "C"
{

    void wgpuSamplerAddRef(WGPUSampler sampler)
    {
        if (sampler)
            sampler->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuSamplerRelease(WGPUSampler sampler)
    {
        if (!sampler)
            return;
        if (sampler->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            if (pe::Sampler *native = sampler->sampler)
            {
                sampler->sampler = nullptr;
                pwgpu::DeferDestroy(sampler->device, [native]() mutable
                                    { pe::Sampler::Destroy(native); });
            }
            if (sampler->device)
                wgpuDeviceRelease(sampler->device);
            delete sampler;
        }
    }

    void wgpuSamplerSetLabel(WGPUSampler sampler, WGPUStringView label)
    {
        if (sampler)
            sampler->label = pwgpu::ToString(label);
    }

} // extern "C"
