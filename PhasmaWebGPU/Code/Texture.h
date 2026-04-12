#pragma once

#include <webgpu/webgpu.h>
#include "API/Image.h"

struct WGPUTextureImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::Image *image = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUTextureUsage usage = WGPUTextureUsage_None;
    WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    WGPUTextureViewDimension textureBindingViewDimension = WGPUTextureViewDimension_Undefined;
    WGPUExtent3D size = {1, 1, 1};
    uint32_t mipLevelCount = 1;
    uint32_t sampleCount = 1;
    bool destroyed = false;
    bool isSwapchain = false;
};

struct WGPUTextureViewImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::ImageView *view = nullptr;
    WGPUTextureImpl *texture = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUTextureViewDimension dimension = WGPUTextureViewDimension_2D;
    uint32_t baseMipLevel = 0;
    uint32_t mipLevelCount = 1;
    uint32_t baseArrayLayer = 0;
    uint32_t arrayLayerCount = 1;
    WGPUTextureAspect aspect = WGPUTextureAspect_All;
};
