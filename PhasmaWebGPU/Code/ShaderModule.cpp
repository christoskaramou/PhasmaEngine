#include "ShaderModule.h"
#include "Utils.h"

extern "C"
{

    void wgpuShaderModuleAddRef(WGPUShaderModule sm)
    {
        if (sm)
            sm->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuShaderModuleRelease(WGPUShaderModule sm)
    {
        if (!sm)
            return;
        if (sm->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            if (sm->shader)
                pe::Shader::Destroy(sm->shader);
            delete sm;
        }
    }

    WGPUFuture wgpuShaderModuleGetCompilationInfo(WGPUShaderModule sm,
                                                  WGPUCompilationInfoCallbackInfo callbackInfo)
    {
        (void)sm;
        if (callbackInfo.callback)
        {
            WGPUCompilationInfo info{};
            info.messageCount = 0;
            info.messages = nullptr;
            callbackInfo.callback(WGPUCompilationInfoRequestStatus_Success, &info,
                                  callbackInfo.userdata1, callbackInfo.userdata2);
        }
        return WGPUFuture{pwgpu::NextFutureId()};
    }

    void wgpuShaderModuleSetLabel(WGPUShaderModule sm, WGPUStringView label)
    {
        if (sm)
            sm->label = pwgpu::ToString(label);
    }

} // extern "C"
