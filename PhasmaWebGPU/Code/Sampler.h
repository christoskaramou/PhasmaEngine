#pragma once

#include <webgpu/webgpu.h>
#include "API/Image.h"

struct WGPUSamplerImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::Sampler *sampler = nullptr;
};
