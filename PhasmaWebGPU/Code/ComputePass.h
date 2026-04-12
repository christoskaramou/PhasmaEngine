#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"

struct WGPUCommandEncoderImpl;

struct WGPUComputePassEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::CommandBuffer *cmd = nullptr;
    WGPUCommandEncoderImpl *parent = nullptr;
    bool ended = false;
};
