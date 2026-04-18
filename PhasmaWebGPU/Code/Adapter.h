#pragma once

#include <webgpu/webgpu.h>
#include "WGPULimits.h"
#include "API/RHI.h"

struct WGPUAdapterChainedCaps
{
    bool shaderFloat16 = false;   // WGPUFeatureName_ShaderF16
    bool depthClipEnable = false; // WGPUFeatureName_DepthClipControl
};

struct WGPUInstanceImpl;

struct WGPUAdapterImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;

    WGPUInstanceImpl *instance = nullptr;
    pe::RHI *rhi = nullptr;
    vk::PhysicalDevice gpu{};

    VkPhysicalDeviceProperties vkProps{};
    VkPhysicalDeviceFeatures vkFeatures{};
    WGPUAdapterChainedCaps chainedCaps{};

    std::vector<WGPUFeatureName> supportedFeatures;

    VkFormat resolvedDepth24Plus = VK_FORMAT_D32_SFLOAT;
    VkFormat resolvedDepth24PlusStencil8 = VK_FORMAT_D32_SFLOAT_S8_UINT;
    bool bgra8UnormStorage = false;
    bool float32Filterable = false;
    bool rg11b10UfloatRenderable = false;
    bool textureFormatsTier1 = false;
    bool textureCompressionBcFullySupported = false;

    std::string deviceName;
    std::string vendorName;
    std::string architecture;
    std::string driverDescription;

    WGPUAdapterType adapterType = WGPUAdapterType_Unknown;
    WGPUBackendType backendType = WGPUBackendType_Vulkan;

    bool consumed = false;
};

void pwgpu_PopulateAdapterFeatureCache(WGPUAdapterImpl &a);
