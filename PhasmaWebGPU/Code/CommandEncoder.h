#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"

struct WGPUCommandBufferImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::CommandBuffer *cmd = nullptr;
    bool submitted = false;
};

struct WGPUCommandEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::CommandBuffer *cmd = nullptr;
    bool finished = false;
    // Set while a pass is being encoded; blocks Begin*Pass and Finish (spec §19).
    bool hasOpenPass = false;
};
