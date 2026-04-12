#include "ShaderModule.h"
#include "Device.h"
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
            WGPUDevice dev = sm->device;
            delete sm;
            if (dev)
                wgpuDeviceRelease(dev);
        }
    }

    WGPUFuture wgpuShaderModuleGetCompilationInfo(WGPUShaderModule sm,
                                                  WGPUCompilationInfoCallbackInfo callbackInfo)
    {
        if (!callbackInfo.callback)
            return WGPUFuture{pwgpu::NextFutureId()};

        if (sm)
            sm->refCount.fetch_add(1, std::memory_order_relaxed);

        std::vector<WGPUCompilationMessage> messages;
        if (sm)
        {
            messages.reserve(sm->compilationMessages.size());
            for (auto &stored : sm->compilationMessages)
            {
                WGPUCompilationMessage msg{};
                msg.nextInChain = nullptr;
                msg.message = pwgpu::ToStringView(stored.message);
                msg.type = stored.type;
                msg.lineNum = stored.lineNum;
                msg.linePos = stored.linePos;
                msg.offset = stored.offset;
                msg.length = stored.length;
                messages.push_back(msg);
            }
        }

        WGPUCompilationInfo info{};
        info.nextInChain = nullptr;
        info.messageCount = messages.size();
        info.messages = messages.empty() ? nullptr : messages.data();

        callbackInfo.callback(WGPUCompilationInfoRequestStatus_Success, &info,
                              callbackInfo.userdata1, callbackInfo.userdata2);

        if (sm)
            wgpuShaderModuleRelease(sm);

        return WGPUFuture{pwgpu::NextFutureId()};
    }

    void wgpuShaderModuleSetLabel(WGPUShaderModule sm, WGPUStringView label)
    {
        if (sm)
            sm->label = pwgpu::ToString(label);
    }

} // extern "C"
