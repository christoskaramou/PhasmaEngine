#pragma once

#include <webgpu/webgpu.h>
#include "WGPULimits.h"
#include "API/RHI.h"

struct WGPUAdapterChainedCaps
{
    bool shaderFloat16 = false;
    bool storageInputOutput16 = false;
    bool depthClipEnable = false;
    bool subgroups = false;
    uint32_t subgroupMinSize = 4;
    uint32_t subgroupMaxSize = 128;
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
    VkFormat resolvedStencil8 = VK_FORMAT_S8_UINT;
    bool bgra8UnormStorage = false;
    bool float32Filterable = false;
    bool rg11b10UfloatRenderable = false;
    bool textureFormatsTier1 = false;
    bool textureFormatsTier2 = false;
    bool textureCompressionBcFullySupported = false;
    bool textureCompressionBCSliced3D = false;
    bool textureCompressionASTCSliced3D = false;
    bool depth32FloatStencil8 = false;
    bool float32Blendable = false;
    bool textureComponentSwizzle = true;
    bool primitiveIndex = false;

    std::string deviceName;
    std::string vendorName;
    std::string architecture;
    std::string driverDescription;

    WGPUAdapterType adapterType = WGPUAdapterType_Unknown;
    WGPUBackendType backendType = WGPUBackendType_Vulkan;

    bool consumed = false;
};

void pwgpu_PopulateAdapterFeatureCache(WGPUAdapterImpl &a);
