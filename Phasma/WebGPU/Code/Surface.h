#pragma once

#include <webgpu/webgpu.h>
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "API/Semaphore.h"
#include "API/RHI.h"

struct WGPUDeviceImpl;
struct WGPUTextureImpl;

struct WGPUSurfaceImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;
    pe::RHI *rhi = nullptr;
    // Cached from rhi; configure and RecreateSurface() replace them, so they
    // are refreshed before every use.
    pe::Surface *surface = nullptr;
    pe::Swapchain *swapchain = nullptr;
    pe::Semaphore *acquireSemaphore = nullptr;
    WGPUSurfaceConfiguration configuration{};
    bool configured = false;
    WGPUTextureImpl *currentTexture = nullptr;
    uint32_t currentImageIndex = UINT32_MAX;
};
