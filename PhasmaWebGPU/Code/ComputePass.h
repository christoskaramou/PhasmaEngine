#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"

struct WGPUCommandEncoderImpl;
struct WGPUDeviceImpl;
struct WGPUComputePipelineImpl;
struct WGPUQuerySetImpl;
struct WGPUBindGroupImpl;

struct WGPUComputePassEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::CommandBuffer *cmd = nullptr;
    WGPUCommandEncoderImpl *parent = nullptr;
    WGPUDeviceImpl *device = nullptr;
    bool ended = false;

    WGPUComputePipelineImpl *pipeline = nullptr;
    uint32_t debugGroupDepth = 0;

    WGPUQuerySetImpl *timestampQuerySet = nullptr;
    uint32_t beginTimestampIndex = UINT32_MAX;
    uint32_t endTimestampIndex = UINT32_MAX;

    std::vector<WGPUComputePipelineImpl *> retainedPipelines;
    std::vector<WGPUBindGroupImpl *> retainedBindGroups;
    std::vector<WGPUBufferImpl *> usedBuffers;
};
