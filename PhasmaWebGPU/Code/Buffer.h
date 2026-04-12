#pragma once

#include <webgpu/webgpu.h>
#include "API/Buffer.h"

struct WGPUDeviceImpl;

struct WGPUBufferImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;

    // Parent device — used for validation-error reporting. Ref-counted via
    // wgpuDeviceAddRef/Release so the device stays alive while any buffer holds
    // a pointer to it.
    WGPUDeviceImpl *device = nullptr;

    pe::Buffer *peBuffer = nullptr;

    // Immutable after creation.
    WGPUBufferUsage usage = WGPUBufferUsage_None;
    uint64_t size = 0;
    bool hostVisible = false;

    // State machine — protected by mapMutex.
    std::mutex mapMutex;
    WGPUBufferMapState mapState = WGPUBufferMapState_Unmapped;
    bool destroyed = false;
    bool invalid = false;

    // Valid iff mapState == Mapped. mappedAtCreation sets [0, size] / Write.
    uint64_t mappedOffset = 0;
    uint64_t mappedSize = 0;
    WGPUMapMode mappedMode = WGPUMapMode_None;

    // Valid iff mapState == Pending.
    WGPUBufferMapCallbackInfo pendingCallback{};
    uint64_t pendingOffset = 0;
    uint64_t pendingSize = 0;
    WGPUMapMode pendingMode = WGPUMapMode_None;
};
