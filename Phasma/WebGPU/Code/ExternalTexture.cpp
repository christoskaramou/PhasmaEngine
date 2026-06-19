#include <webgpu/webgpu.h>
#include "Utils.h"
#include "Device.h"

extern "C"
{

    void wgpuExternalTextureAddRef(WGPUExternalTexture et)
    {
        (void)et;
    }
    void wgpuExternalTextureRelease(WGPUExternalTexture et)
    {
        (void)et;
    }
    void wgpuExternalTextureSetLabel(WGPUExternalTexture et, WGPUStringView label)
    {
        (void)et;
        (void)label;
    }

} // extern "C"
