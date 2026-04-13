#include "ShaderModule.h"
#include "Device.h"
#include "Instance.h"
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
        WGPUInstanceImpl *inst = (sm && sm->device) ? sm->device->instance : nullptr;

        if (!callbackInfo.callback || !inst)
            return inst ? inst->futures.NextId() : WGPUFuture{0};

        // Capture a copy of the stored messages for the deferred callback.
        auto storedMsgs = std::make_shared<std::vector<WGPUCompilationMessageStorage>>();
        if (sm)
            *storedMsgs = sm->compilationMessages;

        auto cb = callbackInfo.callback;
        auto u1 = callbackInfo.userdata1;
        auto u2 = callbackInfo.userdata2;
        return inst->futures.TrackEvent(callbackInfo.mode,
                                        [cb, u1, u2, storedMsgs]()
                                        {
                                            std::vector<WGPUCompilationMessage> messages;
                                            messages.reserve(storedMsgs->size());
                                            for (auto &stored : *storedMsgs)
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
                                            WGPUCompilationInfo info{};
                                            info.nextInChain = nullptr;
                                            info.messageCount = messages.size();
                                            info.messages = messages.empty() ? nullptr : messages.data();
                                            cb(WGPUCompilationInfoRequestStatus_Success, &info, u1, u2);
                                        });
    }

    void wgpuShaderModuleSetLabel(WGPUShaderModule sm, WGPUStringView label)
    {
        if (sm)
            sm->label = pwgpu::ToString(label);
    }

} // extern "C"
