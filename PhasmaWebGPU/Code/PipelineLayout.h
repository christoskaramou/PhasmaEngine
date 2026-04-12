#pragma once

#include <webgpu/webgpu.h>
#include "BindGroup.h"
#include "API/RHI.h"

struct WGPUPipelineLayoutImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    VkPipelineLayout vkLayout = VK_NULL_HANDLE;
    std::vector<WGPUBindGroupLayoutImpl *> bindGroupLayouts;
};
