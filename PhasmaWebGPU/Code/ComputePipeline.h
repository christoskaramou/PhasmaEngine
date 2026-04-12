#pragma once

#include <webgpu/webgpu.h>

struct WGPUDeviceImpl;
struct WGPUPipelineLayoutImpl;
struct WGPUBindGroupLayoutImpl;

struct WGPUComputePipelineImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    bool invalid = false;

    VkPipeline vkPipeline = VK_NULL_HANDLE;
    WGPUPipelineLayoutImpl *layout = nullptr;

    std::string entryPoint;
};
