#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"

struct WGPUDeviceImpl;

struct WGPURenderPipelineImpl;
struct WGPUComputePipelineImpl;
struct WGPUBindGroupImpl;
struct WGPUQuerySetImpl;

struct RetainedResources
{
    std::vector<WGPURenderPipelineImpl *> renderPipelines;
    std::vector<WGPUComputePipelineImpl *> computePipelines;
    std::vector<WGPUBindGroupImpl *> bindGroups;
    std::vector<WGPUQuerySetImpl *> querySets;

    void MergeFrom(RetainedResources &other);
    void ReleaseAll();
};

struct WGPUCommandBufferImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    pe::CommandBuffer *cmd = nullptr;
    bool submitted = false;

    RetainedResources retained;
};

struct WGPUCommandEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    pe::CommandBuffer *cmd = nullptr;
    bool finished = false;
    bool hasOpenPass = false;
    RetainedResources retained;
};
