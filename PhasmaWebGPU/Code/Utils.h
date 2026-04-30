#pragma once

#include <webgpu/webgpu.h>

struct WGPUDeviceImpl;

namespace pwgpu
{

    inline std::string ToString(WGPUStringView sv)
    {
        if (!sv.data)
            return {};
        if (sv.length == WGPU_STRLEN)
            return std::string(sv.data);
        return std::string(sv.data, sv.length);
    }

    inline WGPUStringView ToStringView(const char *str)
    {
        if (!str)
            return {nullptr, 0};
        return {str, WGPU_STRLEN};
    }

    inline WGPUStringView ToStringView(const std::string &s)
    {
        return {s.data(), s.size()};
    }

    template <typename T>
    inline const T *FindChained(const WGPUChainedStruct *chain, WGPUSType sType)
    {
        while (chain)
        {
            if (chain->sType == sType)
                return reinterpret_cast<const T *>(chain);
            chain = chain->next;
        }
        return nullptr;
    }

    template <typename T>
    inline T *FindChained(WGPUChainedStruct *chain, WGPUSType sType)
    {
        while (chain)
        {
            if (chain->sType == sType)
                return reinterpret_cast<T *>(chain);
            chain = chain->next;
        }
        return nullptr;
    }

    // Validates a descriptor's chained-struct list against an allow-list of sTypes.
    // For each node whose sType is not in `allowed`, emits a Validation error on
    // `device` and returns false. Null chains are valid. Returns true only if every
    // node in the chain carries an allowed sType. `callsite` is included in the
    // diagnostic to identify the entry point (e.g. "wgpuDeviceCreateShaderModule").
    // `structName` names the descriptor/struct being validated
    // (e.g. "WGPUShaderModuleDescriptor"); may be null to omit.
    bool ValidateChainedStruct(WGPUDeviceImpl *device,
                               const WGPUChainedStruct *chain,
                               std::initializer_list<WGPUSType> allowed,
                               const char *callsite,
                               const char *structName);

} // namespace pwgpu
