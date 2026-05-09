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

    std::string entryPoint;
    uint64_t workgroupInvocations = 1;
};
