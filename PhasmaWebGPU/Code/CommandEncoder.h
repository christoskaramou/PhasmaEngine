#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"

struct WGPUDeviceImpl;

struct WGPUCommandBufferImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    pe::CommandBuffer *cmd = nullptr;
    bool submitted = false;
};

struct WGPUCommandEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    pe::CommandBuffer *cmd = nullptr;
    bool finished = false;
    // Set while a pass is being encoded; blocks Begin*Pass and Finish (spec §13).
    bool hasOpenPass = false;
};
