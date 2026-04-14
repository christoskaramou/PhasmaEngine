#pragma once

#include <webgpu/webgpu.h>
#include "API/Buffer.h"

struct WGPUDeviceImpl;

enum class BufferInternalState
{
    Available,
    Unavailable,
    Destroyed
};

struct WGPUBufferImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;

    WGPUDeviceImpl *device = nullptr;
    pe::Buffer *peBuffer = nullptr;

    WGPUBufferUsage usage = WGPUBufferUsage_None;
    uint64_t size = 0;
    bool hostVisible = false;

    std::atomic<uint64_t> lastUsageSerial{0};

    std::mutex stateMutex;
    std::atomic<BufferInternalState> internalState{BufferInternalState::Available};
    WGPUBufferMapState mapState = WGPUBufferMapState_Unmapped;
    bool invalid = false;

    uint64_t mappedOffset = 0;
    uint64_t mappedSize = 0;
    WGPUMapMode mappedMode = WGPUMapMode_None;

    std::vector<std::pair<uint64_t, uint64_t>> mappedSubRanges;

    WGPUBufferMapCallbackInfo pendingCallback{};
    uint64_t pendingOffset = 0;
    uint64_t pendingSize = 0;
    WGPUMapMode pendingMode = WGPUMapMode_None;
};
