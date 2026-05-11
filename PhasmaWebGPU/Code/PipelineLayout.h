#pragma once

#include <webgpu/webgpu.h>
#include "API/RHITypes.h"

struct WGPUDeviceImpl;
struct WGPUBindGroupLayoutImpl;

enum class WGPUBindingModel
{
    Classic,
    DescriptorBuffer
};

struct WGPUPipelineLayoutImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;

    PeBackendHandle backendLayout = 0;
    std::vector<WGPUBindGroupLayoutImpl *> bindGroupLayouts;
    std::vector<PeBackendHandle> ownedEmptyBackendLayouts;
    WGPUBindingModel bindingModel = WGPUBindingModel::Classic;
    bool invalid = false;
};
