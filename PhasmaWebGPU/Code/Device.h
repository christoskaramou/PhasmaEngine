#pragma once

#include <webgpu/webgpu.h>
#include "ErrorScope.h"
#include "WGPULimits.h"
#include "API/Vulkan/VulkanHeaders.h"

namespace pe
{
    class RHI;
    class Queue;
} // namespace pe

struct WGPUDeviceImpl;
struct WGPUTextureImpl;
struct WGPUBufferImpl;

namespace pe
{
    class Semaphore;
    class CommandBuffer;
    class ImageView;
} // namespace pe

// A submitted command buffer awaiting GPU completion before it can be recycled.
struct WGPUPendingSubmit
{
    WGPUPendingSubmit() = default;
    WGPUPendingSubmit(pe::CommandBuffer *submittedCmd, uint64_t submittedSerial)
        : cmd(submittedCmd), serial(submittedSerial)
    {
    }

    pe::CommandBuffer *cmd = nullptr;
    uint64_t serial = 0; // Submission serial this cmd was submitted at.
    std::vector<pe::ImageView *> nativeImageViews;
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
    VkFormat resolvedStencil8 = VK_FORMAT_S8_UINT;
    bool supportsDepthClamp = false;
    bool supportsDepthClipEnable = false;

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
    bool suppressReportError = false;

    // Tracked child buffers; destroy() walks this to unmap / mark them destroyed.
    std::unordered_set<WGPUBufferImpl *> trackedBuffers;
    std::mutex trackedBuffersMutex;

    // Lost future: one shared_future handed out to every wgpuDeviceGetLostFuture
    // call; fulfilled in wgpuDeviceDestroy.
    std::promise<void> lostPromise;
    std::shared_future<void> lostFuture;

    struct PendingTextureDeletion
    {
        WGPUTextureImpl *tex;
        uint64_t serial;
    };
    std::vector<PendingTextureDeletion> pendingTextureDeletions;
    std::mutex pendingTextureDeletionsMutex;

    WGPUDeviceImpl() : lostFuture(lostPromise.get_future().share()) {}

    void reportError(WGPUErrorType type, WGPUStringView message);
    void ReclaimCompletedTextureDeletions();
};

namespace pwgpu
{
    // Fires a Validation error directly through device->reportError, bypassing the
    // deferredErrorMessage mechanism. Use ONLY for "no-future-fire-path" cases where
    // no later finish() will surface a deferred message — e.g. a call on an already-
    // finished encoder, or .end() on a phantom pass returned by a failed begin*Pass().
    // See W3C §13.7.
    inline void FireSyncValidation(WGPUDeviceImpl *device, const char *message)
    {
        if (!device || !message)
            return;
        device->reportError(WGPUErrorType_Validation, WGPUStringView{message, WGPU_STRLEN});
    }
} // namespace pwgpu
