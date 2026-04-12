#include "Buffer.h"
#include "Utils.h"

extern "C"
{

    void wgpuBufferAddRef(WGPUBuffer buffer)
    {
        if (buffer)
            buffer->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuBufferRelease(WGPUBuffer buffer)
    {
        if (!buffer)
            return;
        if (buffer->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            if (buffer->buffer && !buffer->destroyed)
                pe::Buffer::Destroy(buffer->buffer);
            delete buffer;
        }
    }

    void wgpuBufferDestroy(WGPUBuffer buffer)
    {
        if (!buffer || buffer->destroyed)
            return;
        buffer->destroyed = true;
        if (buffer->buffer)
        {
            pe::Buffer::Destroy(buffer->buffer);
            buffer->buffer = nullptr;
        }
    }

    uint64_t wgpuBufferGetSize(WGPUBuffer buffer)
    {
        return buffer ? buffer->size : 0;
    }

    WGPUBufferUsage wgpuBufferGetUsage(WGPUBuffer buffer)
    {
        return buffer ? buffer->usage : WGPUBufferUsage_None;
    }

    WGPUBufferMapState wgpuBufferGetMapState(WGPUBuffer buffer)
    {
        return buffer ? buffer->mapState : WGPUBufferMapState_Unmapped;
    }

    void *wgpuBufferGetMappedRange(WGPUBuffer buffer, size_t offset, size_t size)
    {
        (void)size;
        if (!buffer || !buffer->buffer || buffer->mapState != WGPUBufferMapState_Mapped)
            return nullptr;
        uint8_t *data = static_cast<uint8_t *>(buffer->buffer->Data());
        return data ? data + offset : nullptr;
    }

    void const *wgpuBufferGetConstMappedRange(WGPUBuffer buffer, size_t offset, size_t size)
    {
        (void)size;
        if (!buffer || !buffer->buffer || buffer->mapState != WGPUBufferMapState_Mapped)
            return nullptr;
        const uint8_t *data = static_cast<const uint8_t *>(buffer->buffer->Data());
        return data ? data + offset : nullptr;
    }

    WGPUStatus wgpuBufferReadMappedRange(WGPUBuffer buffer, size_t offset, void *data, size_t size)
    {
        if (!buffer || !buffer->buffer || !data || buffer->mapState != WGPUBufferMapState_Mapped)
            return WGPUStatus_Error;
        if (offset + size > buffer->size)
            return WGPUStatus_Error;
        const uint8_t *src = static_cast<const uint8_t *>(buffer->buffer->Data());
        if (!src)
            return WGPUStatus_Error;
        std::memcpy(data, src + offset, size);
        return WGPUStatus_Success;
    }

    WGPUStatus wgpuBufferWriteMappedRange(WGPUBuffer buffer, size_t offset, void const *data, size_t size)
    {
        if (!buffer || !buffer->buffer || !data || buffer->mapState != WGPUBufferMapState_Mapped)
            return WGPUStatus_Error;
        if (offset + size > buffer->size)
            return WGPUStatus_Error;
        uint8_t *dst = static_cast<uint8_t *>(buffer->buffer->Data());
        if (!dst)
            return WGPUStatus_Error;
        std::memcpy(dst + offset, data, size);
        return WGPUStatus_Success;
    }

    WGPUFuture wgpuBufferMapAsync(WGPUBuffer buffer,
                                  WGPUMapMode mode,
                                  size_t offset,
                                  size_t size,
                                  WGPUBufferMapCallbackInfo callbackInfo)
    {
        (void)mode;
        (void)offset;
        (void)size;
        if (buffer && buffer->buffer)
        {
            buffer->buffer->Map();
            buffer->mapState = WGPUBufferMapState_Mapped;
            if (callbackInfo.callback)
                callbackInfo.callback(WGPUMapAsyncStatus_Success, {nullptr, 0},
                                      callbackInfo.userdata1, callbackInfo.userdata2);
        }
        else
        {
            if (callbackInfo.callback)
                callbackInfo.callback(WGPUMapAsyncStatus_Error, pwgpu::ToStringView("Buffer not initialized"),
                                      callbackInfo.userdata1, callbackInfo.userdata2);
        }
        return WGPUFuture{pwgpu::NextFutureId()};
    }

    void wgpuBufferUnmap(WGPUBuffer buffer)
    {
        if (!buffer || !buffer->buffer || buffer->mapState != WGPUBufferMapState_Mapped)
            return;
        buffer->buffer->Unmap();
        buffer->mapState = WGPUBufferMapState_Unmapped;
    }

    void wgpuBufferSetLabel(WGPUBuffer buffer, WGPUStringView label)
    {
        if (buffer)
            buffer->label = pwgpu::ToString(label);
    }

} // extern "C"
