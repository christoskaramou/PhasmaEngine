#pragma once

#include <atomic>
#include <string>
#include <unordered_set>

#include <webgpu/webgpu.h>
#include "API/RHITypes.h"

struct WGPUDeviceImpl;

struct WGPUQuerySetImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    PeBackendHandle backendQueryPool = 0;
    WGPUQueryType type = WGPUQueryType_Occlusion;
    uint32_t count = 0;
    bool destroyed = false;
    bool invalid = false;
    // Waiting on an unavailable slot can stall forever; resolveQuerySet copies only these.
    std::unordered_set<uint32_t> beganIndices;
};
