#pragma once

#include <webgpu/webgpu.h>
#include "API/Buffer.h"

struct WGPUBufferImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::Buffer *buffer = nullptr;
    WGPUBufferUsage usage = WGPUBufferUsage_None;
    uint64_t size = 0;
    WGPUBufferMapState mapState = WGPUBufferMapState_Unmapped;
    bool destroyed = false;
};
