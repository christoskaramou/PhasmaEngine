#pragma once

#include <webgpu/webgpu.h>
#include "ErrorScope.h"
#include "WGPULimits.h"

namespace pe
{
    class RHI;
    class Queue;
} // namespace pe

struct WGPUDeviceImpl;

struct WGPUQueueImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;

    pe::Queue *peQueue = nullptr;
};

struct WGPUDeviceImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;

    WGPUQueueImpl *queue = nullptr;

    pe::RHI *rhi = nullptr;
    pe::Queue *peQueue = nullptr;

    std::vector<WGPUFeatureName> features;
    WGPULimits limits{};
    VkFormat resolvedDepth24Plus = VK_FORMAT_D32_SFLOAT;
    VkFormat resolvedDepth24PlusStencil8 = VK_FORMAT_D32_SFLOAT_S8_UINT;

    std::string adapterVendor;
    std::string adapterArchitecture;
    std::string adapterDeviceName;
    std::string adapterDescription;
    WGPUAdapterType adapterType = WGPUAdapterType_Unknown;
    WGPUBackendType adapterBackend = WGPUBackendType_Vulkan;
    uint32_t adapterVendorID = 0;
    uint32_t adapterDeviceID = 0;

    std::vector<pwgpu::ErrorScope> errorScopeStack;
    std::mutex errorScopeMutex;
    WGPUUncapturedErrorCallbackInfo uncapturedErrorCallbackInfo{};
    WGPUDeviceLostCallbackInfo deviceLostCallbackInfo{};
    bool destroyed = false;

    void reportError(WGPUErrorType type, WGPUStringView message);
};
