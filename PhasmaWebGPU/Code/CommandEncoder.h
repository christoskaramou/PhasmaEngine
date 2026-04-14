#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"

struct WGPUDeviceImpl;

struct WGPURenderPipelineImpl;
struct WGPUComputePipelineImpl;
struct WGPUBindGroupImpl;
struct WGPUQuerySetImpl;
struct WGPUTextureViewImpl;
struct WGPURenderBundleImpl;

struct WGPUBufferImpl;

struct RetainedResources
{
    std::vector<WGPURenderPipelineImpl *> renderPipelines;
    std::vector<WGPUComputePipelineImpl *> computePipelines;
    std::vector<WGPUBindGroupImpl *> bindGroups;
    std::vector<WGPUQuerySetImpl *> querySets;
    std::vector<WGPUTextureViewImpl *> textureViews;
    std::vector<WGPURenderBundleImpl *> renderBundles;
    std::vector<WGPUBufferImpl *> usedBuffers; // buffers touched by this command encoder

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
    bool invalid = false;

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
    bool invalid = false;
    RetainedResources retained;
    std::vector<WGPUQuerySetImpl *> resetOcclusionQuerySets;
};
