#pragma once

#include <webgpu/webgpu.h>
#include <functional>
#include "UsageTracker.h"

struct WGPUDeviceImpl;
struct WGPURenderPipelineImpl;
struct WGPUBindGroupImpl;
struct WGPUBufferImpl;

using BundleCommand = std::function<void(vk::CommandBuffer)>;

struct WGPURenderBundleImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    bool invalid = false;

    std::vector<WGPUTextureFormat> colorFormats;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    uint32_t sampleCount = 1;
    bool depthReadOnly = false;
    bool stencilReadOnly = false;
    uint64_t drawCount = 0;

    std::vector<BundleCommand> commands;

    std::vector<WGPURenderPipelineImpl *> retainedPipelines;
    std::vector<WGPUBindGroupImpl *> retainedBindGroups;
    std::vector<WGPUBufferImpl *> retainedBuffers;

    pwgpu::UsageScope usageScope;
    bool usageScopeValid = true;
};

struct WGPURenderBundleEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    bool invalid = false;
    bool finished = false;

    std::vector<WGPUTextureFormat> colorFormats;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    uint32_t sampleCount = 1;
    bool depthReadOnly = false;
    bool stencilReadOnly = false;

    WGPURenderPipelineImpl *pipeline = nullptr;
    uint32_t debugGroupDepth = 0;
    uint64_t drawCount = 0;

    WGPUBufferImpl *indexBuffer = nullptr;
    WGPUIndexFormat indexFormat = WGPUIndexFormat_Undefined;
    uint64_t indexBufferSize = 0;

    std::vector<BundleCommand> commands;

    std::vector<WGPURenderPipelineImpl *> retainedPipelines;
    std::vector<WGPUBindGroupImpl *> retainedBindGroups;
    std::vector<WGPUBufferImpl *> retainedBuffers;

    pwgpu::UsageScope usageScope;
    bool usageScopeValid = true;

    std::vector<WGPUBindGroupImpl *> currentBindGroups;
};
