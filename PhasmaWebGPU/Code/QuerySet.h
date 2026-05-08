#pragma once

#include <webgpu/webgpu.h>
#include "API/RHI.h"
#include "API/Vulkan/VulkanHeaders.h"

struct WGPUDeviceImpl;

struct WGPUQuerySetImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    VkQueryPool queryPool = VK_NULL_HANDLE;
    WGPUQueryType type = WGPUQueryType_Occlusion;
    uint32_t count = 0;
    bool destroyed = false;
    bool invalid = false;
    // eWait on an unbegun slot stalls forever; resolveQuerySet copies only these.
    std::unordered_set<uint32_t> beganIndices;
};
