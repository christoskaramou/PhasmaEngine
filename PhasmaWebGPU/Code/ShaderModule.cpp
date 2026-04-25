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
                for (uint32_t w = i + 3; w < i + wordCount; ++w)
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

    // WGSL frontend pushes reflection meta in here via flat (group, binding) pairs.

#if defined(_WIN32)
#define PWGPU_EXT_EXPORT __declspec(dllexport)
#else
#define PWGPU_EXT_EXPORT __attribute__((visibility("default")))
#endif

    PWGPU_EXT_EXPORT void pwgpuShaderModuleResetReflection(WGPUShaderModule sm)
    {
        if (!sm)
            return;
        sm->reflection = WGPUShaderReflectionMeta{};
        sm->reflection.present = true;
        // Reflection is authoritative when present (custom WGSL entry-point names survive
        // even if the naga-panic fallback substitutes a "main"-only placeholder SPIR-V).
        // AddEntryPointUsed will repopulate sm->entryPoints from the reflection side-channel.
        sm->entryPoints.clear();
    }

    PWGPU_EXT_EXPORT void pwgpuShaderModuleSetComparisonSamplers(WGPUShaderModule sm,
                                                                 const uint32_t *pairs,
                                                                 uint32_t pairCount)
    {
        if (!sm)
            return;
        sm->reflection.comparisonSamplers.clear();
        sm->reflection.comparisonSamplers.reserve(pairCount);
        for (uint32_t i = 0; i < pairCount; ++i)
        {
            sm->reflection.comparisonSamplers.push_back(
                WGPUShaderReflectionMeta::Binding{pairs[2 * i], pairs[2 * i + 1]});
        }
    }

    PWGPU_EXT_EXPORT void pwgpuShaderModuleAddEntryPointUsed(WGPUShaderModule sm,
                                                             const char *name,
                                                             const char *stage,
                                                             const uint32_t *pairs,
                                                             uint32_t pairCount)
    {
        if (!sm || !name || !stage)
            return;
        WGPUShaderReflectionMeta::EntryPoint ep;
        ep.name = name;
        ep.stage = stage;
        ep.staticallyUsed.reserve(pairCount);
        for (uint32_t i = 0; i < pairCount; ++i)
        {
            ep.staticallyUsed.push_back(
                WGPUShaderReflectionMeta::Binding{pairs[2 * i], pairs[2 * i + 1]});
        }
        sm->reflection.entryPoints.push_back(std::move(ep));

        // Mirror to sm->entryPoints so pipeline creation can resolve custom WGSL entry
        // names even when the SPIR-V parse only sees a "main" placeholder (naga panic
        // fallback path). SPIR-V ExecutionModel: Vertex=0, Fragment=4, GLCompute=5.
        WGPUShaderModuleImpl::EntryPoint modEp;
        modEp.name = name;
        std::string stageStr(stage);
        if (stageStr == "vertex")
            modEp.executionModel = 0;
        else if (stageStr == "fragment")
            modEp.executionModel = 4;
        else if (stageStr == "compute")
            modEp.executionModel = 5;
        else
            modEp.executionModel = 0;
        sm->entryPoints.push_back(std::move(modEp));
    }

    PWGPU_EXT_EXPORT void pwgpuShaderModuleAddOverride(WGPUShaderModule sm,
                                                       const char *identifier,
                                                       const char *name,
                                                       const char *typeStr,
                                                       uint32_t hasDefault,
                                                       uint32_t staticallyUsed)
    {
        if (!sm || !identifier)
            return;
        WGPUShaderReflectionMeta::Override ov;
        ov.identifier = identifier;
        ov.name = name ? name : "";
        ov.type = typeStr ? typeStr : "";
        ov.hasDefault = hasDefault != 0;
        ov.staticallyUsed = staticallyUsed != 0;
        sm->reflection.overrides.push_back(std::move(ov));
    }

    PWGPU_EXT_EXPORT uint32_t pwgpuShaderModuleValidateOverrides(WGPUShaderModule sm)
    {
        if (!sm || !sm->device)
            return 0;
        std::unordered_set<std::string> seenIds;
        std::unordered_set<std::string> seenNames;
        for (const auto &ov : sm->reflection.overrides)
        {
            bool isNumeric = !ov.identifier.empty() &&
                             std::all_of(ov.identifier.begin(), ov.identifier.end(),
                                         [](char c)
                                         { return c >= '0' && c <= '9'; });
            if (isNumeric)
            {
                if (!seenIds.insert(ov.identifier).second)
                {
                    std::string msg = "createShaderModule: duplicate override @id(" +
                                      ov.identifier + ")";
                    sm->device->reportError(WGPUErrorType_Validation,
                                            pwgpu::ToStringView(msg));
                    sm->invalid = true;
                    WGPUCompilationMessageStorage cm;
                    cm.type = WGPUCompilationMessageType_Error;
                    cm.message = msg;
                    sm->compilationMessages.push_back(std::move(cm));
                    return 1;
                }
            }
            if (!ov.name.empty())
            {
                if (!seenNames.insert(ov.name).second)
                {
                    std::string msg = "createShaderModule: duplicate override name '" +
                                      ov.name + "'";
                    sm->device->reportError(WGPUErrorType_Validation,
                                            pwgpu::ToStringView(msg));
                    sm->invalid = true;
                    WGPUCompilationMessageStorage cm;
                    cm.type = WGPUCompilationMessageType_Error;
                    cm.message = msg;
                    sm->compilationMessages.push_back(std::move(cm));
                    return 1;
                }
            }
        }
        return 0;
    }

} // extern "C"
