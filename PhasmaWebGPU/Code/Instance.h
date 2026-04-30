#pragma once

#include <webgpu/webgpu.h>
#include "FutureRegistry.h"

namespace pe
{
    class RHI;
}

struct WGPUInstanceImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pwgpu::FutureRegistry futures;

    pe::RHI *rhi = nullptr;
    bool ownsRhi = false;
};
