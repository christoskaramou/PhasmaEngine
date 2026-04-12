#pragma once

#include <webgpu/webgpu.h>
#include "API/RHI.h"

struct WGPUQuerySetImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    VkQueryPool queryPool = VK_NULL_HANDLE;
    WGPUQueryType type = WGPUQueryType_Occlusion;
    uint32_t count = 0;
    bool destroyed = false;
};
