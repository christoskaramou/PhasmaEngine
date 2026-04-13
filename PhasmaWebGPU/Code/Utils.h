#pragma once

#include <webgpu/webgpu.h>

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

} // namespace pwgpu
