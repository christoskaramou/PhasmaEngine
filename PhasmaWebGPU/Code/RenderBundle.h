#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"

struct WGPURenderBundleImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::CommandBuffer *cmd = nullptr;
};

struct WGPURenderBundleEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::CommandBuffer *cmd = nullptr;
    bool finished = false;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    std::vector<WGPUTextureFormat> colorFormats;
    uint32_t sampleCount = 1;
};
