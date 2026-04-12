#pragma once

#include <webgpu/webgpu.h>

struct WGPUDeviceImpl;
struct WGPUPipelineLayoutImpl;
struct WGPUBindGroupLayoutImpl;

struct WGPURenderPipelineImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    bool invalid = false;

    VkPipeline vkPipeline = VK_NULL_HANDLE;
    WGPUPipelineLayoutImpl *layout = nullptr;

    bool writesDepth = false;
    bool writesStencil = false;
    uint32_t sampleCount = 1;
    std::vector<WGPUTextureFormat> colorFormats;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    std::string vertexEntryPoint;
    std::string fragmentEntryPoint;
};
