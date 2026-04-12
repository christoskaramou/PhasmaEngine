#include "Device.h"
#include "Buffer.h"
#include "Texture.h"
#include "Utils.h"

extern "C"
{

    void wgpuQueueAddRef(WGPUQueue queue)
    {
        if (queue)
            queue->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuQueueRelease(WGPUQueue queue)
    {
        if (!queue)
            return;
        if (queue->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete queue;
    }

    void wgpuQueueSubmit(WGPUQueue queue, size_t commandCount, WGPUCommandBuffer const *commands)
    {
        (void)queue;
        (void)commandCount;
        (void)commands;
    }

    void wgpuQueueWriteBuffer(WGPUQueue queue, WGPUBuffer buffer,
                              uint64_t bufferOffset, void const *data, size_t size)
    {
        (void)queue;
        if (!buffer || !buffer->peBuffer || !data || buffer->destroyed)
            return;
        if (!buffer->hostVisible)
            return;
        pe::BufferRange range{const_cast<void *>(data), size, static_cast<size_t>(bufferOffset)};
        buffer->peBuffer->Copy(1, &range, false);
    }

    void wgpuQueueWriteTexture(WGPUQueue queue,
                               WGPUTexelCopyTextureInfo const *destination,
                               void const *data,
                               size_t dataSize,
                               WGPUTexelCopyBufferLayout const *dataLayout,
                               WGPUExtent3D const *writeSize)
    {
        (void)queue;
        (void)destination;
        (void)data;
        (void)dataSize;
        (void)dataLayout;
        (void)writeSize;
    }

    WGPUFuture wgpuQueueOnSubmittedWorkDone(WGPUQueue queue, WGPUQueueWorkDoneCallbackInfo callbackInfo)
    {
        (void)queue;
        if (callbackInfo.callback)
            callbackInfo.callback(WGPUQueueWorkDoneStatus_Success, {nullptr, 0}, callbackInfo.userdata1, callbackInfo.userdata2);
        return WGPUFuture{pwgpu::NextFutureId()};
    }

    void wgpuQueueSetLabel(WGPUQueue queue, WGPUStringView label)
    {
        if (queue)
            queue->label = pwgpu::ToString(label);
    }

} // extern "C"
