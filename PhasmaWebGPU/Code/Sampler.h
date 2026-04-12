#pragma once

#include <webgpu/webgpu.h>
#include "API/Image.h"

struct WGPUDeviceImpl;

struct WGPUSamplerImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    pe::Sampler *sampler = nullptr;
    WGPUDeviceImpl *device = nullptr;

    WGPUAddressMode addressModeU = WGPUAddressMode_ClampToEdge;
    WGPUAddressMode addressModeV = WGPUAddressMode_ClampToEdge;
    WGPUAddressMode addressModeW = WGPUAddressMode_ClampToEdge;
    WGPUFilterMode magFilter = WGPUFilterMode_Nearest;
    WGPUFilterMode minFilter = WGPUFilterMode_Nearest;
    WGPUMipmapFilterMode mipmapFilter = WGPUMipmapFilterMode_Nearest;
    float lodMinClamp = 0.0f;
    float lodMaxClamp = 32.0f;
    WGPUCompareFunction compare = WGPUCompareFunction_Undefined;
    uint16_t maxAnisotropy = 1;

    bool isComparison = false;
    bool isFiltering = false;
    bool invalid = false;
};
