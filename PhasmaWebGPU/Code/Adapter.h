#pragma once

#include <webgpu/webgpu.h>
#include "WGPULimits.h"
#include "API/RHI.h"

struct WGPUAdapterChainedCaps
{
    bool shaderFloat16 = false;   // WGPUFeatureName_ShaderF16
    bool depthClipEnable = false; // WGPUFeatureName_DepthClipControl
};

struct WGPUAdapterImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;

    pe::RHI *rhi = nullptr;
    vk::PhysicalDevice gpu{};

    VkPhysicalDeviceProperties vkProps{};
    VkPhysicalDeviceFeatures vkFeatures{};
    WGPUAdapterChainedCaps chainedCaps{};

    std::vector<WGPUFeatureName> supportedFeatures;

    std::string deviceName;
    std::string vendorName;
    std::string architecture;
    std::string driverDescription;

    WGPUAdapterType adapterType = WGPUAdapterType_Unknown;
    WGPUBackendType backendType = WGPUBackendType_Vulkan;

    bool consumed = false;
};

void pwgpu_PopulateAdapterFeatureCache(WGPUAdapterImpl &a);
