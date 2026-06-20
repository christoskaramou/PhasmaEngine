#pragma once

#include <webgpu/webgpu.h>
#include "API/RHITypes.h"

namespace pe
{
    class QueryPool;
}

struct WGPUDeviceImpl;

struct WGPUQuerySetImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    // Neutral RHI query pool; owns the native VkQueryPool / ID3D12QueryHeap. Null until created.
    pe::QueryPool *backendQueryPool = nullptr;
    WGPUQueryType type = WGPUQueryType_Occlusion;
    uint32_t count = 0;
    bool destroyed = false;
    bool invalid = false;
    // Waiting on an unavailable slot can stall forever; resolveQuerySet copies only these.
    std::unordered_set<uint32_t> beganIndices;
};
