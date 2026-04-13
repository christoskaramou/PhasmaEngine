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

namespace pe
{
    class Semaphore;
    class CommandBuffer;
} // namespace pe

// A submitted command buffer awaiting GPU completion before it can be recycled.
struct WGPUPendingSubmit
{
    pe::CommandBuffer *cmd = nullptr;
    uint64_t serial = 0; // Submission serial this cmd was submitted at.
};

struct WGPUQueueImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;
    WGPUDeviceImpl *device = nullptr;

    pe::Queue *peQueue = nullptr;

    // Last submission serial submitted to this queue.
    // Used by Queue-timeline futures to know what serial to wait on.
    std::atomic<uint64_t> lastSubmissionSerial{0};

    // Command buffers awaiting GPU completion before recycling.
    std::mutex pendingMutex;
    std::vector<WGPUPendingSubmit> pendingSubmits;

    // Convenience: returns the timeline semaphore from peQueue (may be null).
    pe::Semaphore *GetSemaphore() const;

    // Recycle any command buffers whose GPU work has finished.
    void RecyclePendingSubmits();
};

struct WGPUInstanceImpl;

struct WGPUDeviceImpl
{
    std::atomic<uint32_t> refCount{1};
    std::string label;

    WGPUInstanceImpl *instance = nullptr;
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
