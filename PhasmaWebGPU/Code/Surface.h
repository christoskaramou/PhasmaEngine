#pragma once

#include <webgpu/webgpu.h>
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "API/RHI.h"

struct WGPUSurfaceImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::Surface *surface = nullptr;
    pe::Swapchain *swapchain = nullptr;
    WGPUSurfaceConfiguration configuration{};
    bool configured = false;
};
