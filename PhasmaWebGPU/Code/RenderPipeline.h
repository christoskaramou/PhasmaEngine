#pragma once

#include <webgpu/webgpu.h>
#include "API/Pipeline.h"

struct WGPUBindGroupLayoutImpl;

struct WGPURenderPipelineImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::Pipeline *pipeline = nullptr;
    pe::PassInfo *passInfo = nullptr;
    std::vector<WGPUBindGroupLayoutImpl *> bindGroupLayouts;
};
