#pragma once

#include <webgpu/webgpu.h>
#include "API/RHITypes.h"

struct WGPUDeviceImpl;
struct WGPUPipelineLayoutImpl;
struct WGPUBindGroupLayoutImpl;

struct WGPUComputePipelineImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    bool invalid = false;

    PeBackendHandle backendPipeline = 0;
    WGPUPipelineLayoutImpl *layout = nullptr;
    // The 32-bit immediate-data slots the pipeline's shaders read; a draw or dispatch needs every one set.
    uint64_t immediateSlots = 0;

    std::string entryPoint;
    uint64_t workgroupInvocations = 1;
};
