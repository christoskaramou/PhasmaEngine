#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"

struct WGPUCommandEncoderImpl;
struct WGPUDeviceImpl;
struct WGPURenderPipelineImpl;

struct WGPUBindGroupImpl;

struct WGPURenderPassEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::CommandBuffer *cmd = nullptr;
    WGPUCommandEncoderImpl *parent = nullptr;
    WGPUDeviceImpl *device = nullptr;
    bool ended = false;

    WGPURenderPipelineImpl *pipeline = nullptr;
    uint32_t debugGroupDepth = 0;
    bool renderingActive = false;

    std::vector<WGPURenderPipelineImpl *> retainedPipelines;
    std::vector<WGPUBindGroupImpl *> retainedBindGroups;
};
