#include "ShaderModule.h"
#include "Device.h"
#include "Instance.h"
#include "Utils.h"

namespace pwgpu
{
    bool ParseSpirvEntryPoints(const uint32_t *code, size_t codeSize,
                               std::vector<WGPUShaderModuleImpl::EntryPoint> &out)
    {
        out.clear();
        // Header: magic, version, generator, bound, schema (5 words).
        if (!code || codeSize < 5 || code[0] != 0x07230203u)
            return false;

        size_t i = 5;
        while (i < codeSize)
        {
            uint32_t word0 = code[i];
            uint32_t wordCount = word0 >> 16;
            uint32_t opcode = word0 & 0xFFFFu;
            if (wordCount == 0 || i + wordCount > codeSize)
                return false;

            // OpEntryPoint = 15. Layout: word0 | ExecutionModel | EntryId |
            // Name (null-terminated literal, packed 4 chars/word) | Interface IDs...
            if (opcode == 15 && wordCount >= 4)
            {
                WGPUShaderModuleImpl::EntryPoint ep;
                ep.executionModel = code[i + 1];
                // Reconstruct the name from packed words, stopping at NUL.
                std::string name;
                for (size_t w = i + 3; w < i + wordCount; ++w)
                {
                    uint32_t v = code[w];
                    for (int b = 0; b < 4; ++b)
                    {
                        char c = static_cast<char>((v >> (b * 8)) & 0xFFu);
                        if (c == '\0')
                        {
                            w = i + wordCount; // break outer loop
                            break;
                        }
                        name.push_back(c);
                    }
                }
                ep.name = std::move(name);
                out.push_back(std::move(ep));
            }
            i += wordCount;
        }
        return true;
    }
} // namespace pwgpu

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
