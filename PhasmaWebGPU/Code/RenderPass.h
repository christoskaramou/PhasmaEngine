#pragma once

#include <webgpu/webgpu.h>
#include "API/Command.h"
#include "API/Image.h"

struct WGPUCommandEncoderImpl;
struct WGPUDeviceImpl;
struct WGPURenderPipelineImpl;

struct WGPUBindGroupImpl;

struct WGPUTextureViewImpl;
struct WGPUQuerySetImpl;

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

    uint32_t attachmentWidth = 0;
    uint32_t attachmentHeight = 0;
    bool depthReadOnly = false;
    bool stencilReadOnly = false;

    WGPUQuerySetImpl *timestampQuerySet = nullptr;
    uint32_t beginTimestampIndex = UINT32_MAX;
    uint32_t endTimestampIndex = UINT32_MAX;

    std::vector<WGPUTextureViewImpl *> retainedViews;
    std::vector<pe::ImageView *> ownedSliceViews;

    std::vector<WGPURenderPipelineImpl *> retainedPipelines;
    std::vector<WGPUBindGroupImpl *> retainedBindGroups;
};
