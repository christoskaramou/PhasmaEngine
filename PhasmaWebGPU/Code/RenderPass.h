#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"
#include "API/Image.h"
#include "RenderPipeline.h"
#include "UsageTracker.h"

struct WGPUCommandEncoderImpl;
struct WGPUDeviceImpl;
struct WGPUBindGroupImpl;
struct WGPUTextureViewImpl;
struct WGPUQuerySetImpl;
struct WGPURenderBundleImpl;

struct WGPURenderPassEncoderImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::CommandBuffer *cmd = nullptr;
    WGPUCommandEncoderImpl *parent = nullptr;
    WGPUDeviceImpl *device = nullptr;
    bool ended = false;
    bool invalid = false;
    bool wasOpened = true;
    // Invalid pass whose error must surface at queue.submit(), not finish().
    bool deferredResourceError = false;

    WGPURenderPipelineImpl *pipeline = nullptr;
    uint32_t debugGroupDepth = 0;
    bool renderingActive = false;
    bool bindingStateInvalidated = false;

    uint32_t attachmentWidth = 0;
    uint32_t attachmentHeight = 0;
    bool depthReadOnly = false;
    bool stencilReadOnly = false;

    std::vector<WGPUTextureFormat> colorFormats;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    uint32_t sampleCount = 1;

    WGPUQuerySetImpl *timestampQuerySet = nullptr;
    uint32_t beginTimestampIndex = UINT32_MAX;
    uint32_t endTimestampIndex = UINT32_MAX;

    WGPUQuerySetImpl *occlusionQuerySet = nullptr;
    bool occlusionQueryActive = false;
    uint32_t activeOcclusionIndex = UINT32_MAX;
    std::unordered_set<uint32_t> usedOcclusionIndices;

    std::vector<WGPUTextureViewImpl *> retainedViews;
    std::vector<pe::ImageView *> ownedSliceViews;

    std::vector<WGPURenderPipelineImpl *> retainedPipelines;
    std::vector<WGPUBindGroupImpl *> retainedBindGroups;
    std::vector<WGPURenderBundleImpl *> retainedBundles;
    std::vector<WGPUBufferImpl *> usedBuffers;

    pwgpu::UsageScope usageScope;
    bool usageScopeValid = true;

    WGPUBufferImpl *indexBuffer = nullptr;
    WGPUIndexFormat indexFormat = WGPUIndexFormat_Undefined;
    uint64_t indexBufferSize = 0;

    uint64_t drawCount = 0;
    uint64_t maxDrawCount = 50000000;

    std::vector<WGPUBindGroupImpl *> currentBindGroups;

    std::vector<VertexBufferBinding> boundVertexBuffers;
};
