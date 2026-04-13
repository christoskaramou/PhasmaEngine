#include "Buffer.h"
#include "Device.h"
#include "Instance.h"
#include "Utils.h"

namespace
{
    bool ResolveRange(uint64_t bufferSize, size_t offset, size_t size, uint64_t &outSize)
    {
        if (offset > bufferSize)
            return false;
        if (size == WGPU_WHOLE_MAP_SIZE)
            outSize = bufferSize - offset;
        else
            outSize = size;
        return true;
    }

    void ReportBufferValidationError(WGPUBuffer buffer, const char *what)
    {
        if (!buffer || !buffer->device)
            return;
        std::string msg = std::string("wgpuBuffer: ") + what;
        buffer->device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(msg));
    }

    void FireMapCallback(const WGPUBufferMapCallbackInfo &info,
                         WGPUMapAsyncStatus status,
                         const char *message)
    {
        if (!info.callback)
            return;
        info.callback(status,
                      message ? pwgpu::ToStringView(message) : WGPUStringView{nullptr, 0},
                      info.userdata1,
                      info.userdata2);
    }
} // namespace

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
            if (buffer->peBuffer && !buffer->destroyed)
            {
                if (buffer->hostVisible)
                    buffer->peBuffer->Unmap();
                pe::Buffer::Destroy(buffer->peBuffer);
            }
            WGPUDevice dev = buffer->device;
            delete buffer;
            if (dev)
                wgpuDeviceRelease(dev);
        }
    }

    void wgpuBufferDestroy(WGPUBuffer buffer)
    {
        if (!buffer)
            return;

        WGPUBufferMapCallbackInfo pendingCallback{};
        bool hadPending = false;
        {
            std::lock_guard<std::mutex> lock(buffer->mapMutex);
            if (buffer->destroyed)
                return;
            if (buffer->mapState == WGPUBufferMapState_Pending)
            {
                pendingCallback = buffer->pendingCallback;
                hadPending = true;
                buffer->pendingCallback = {};
            }
            buffer->destroyed = true;
            buffer->mapState = WGPUBufferMapState_Unmapped;
            buffer->mappedOffset = 0;
            buffer->mappedSize = 0;
            buffer->mappedMode = WGPUMapMode_None;

            if (buffer->peBuffer)
            {
                if (buffer->hostVisible)
                    buffer->peBuffer->Unmap();
                pe::Buffer::Destroy(buffer->peBuffer);
                buffer->peBuffer = nullptr;
            }
        }

        if (hadPending)
            FireMapCallback(pendingCallback, WGPUMapAsyncStatus_Aborted, "Buffer destroyed");
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
        if (!buffer)
            return WGPUBufferMapState_Unmapped;
        std::lock_guard<std::mutex> lock(buffer->mapMutex);
        return buffer->mapState;
    }

    void *wgpuBufferGetMappedRange(WGPUBuffer buffer, size_t offset, size_t size)
    {
        if (!buffer)
            return nullptr;

        const char *errorText = nullptr;
        void *result = nullptr;
        {
            std::lock_guard<std::mutex> lock(buffer->mapMutex);
            uint64_t rangeSize = 0;

            if (buffer->destroyed)
                errorText = "getMappedRange on destroyed buffer";
            else if (buffer->mapState != WGPUBufferMapState_Mapped)
                errorText = "getMappedRange called on an unmapped buffer";
            else if (!ResolveRange(buffer->size, offset, size, rangeSize))
                errorText = "getMappedRange offset exceeds buffer size";
            else if ((offset % 8) != 0)
                errorText = "getMappedRange offset must be a multiple of 8";
            else if ((rangeSize % 4) != 0)
                errorText = "getMappedRange size must be a multiple of 4";
            else if (offset < buffer->mappedOffset ||
                     offset + rangeSize > buffer->mappedOffset + buffer->mappedSize)
                errorText = "getMappedRange range is outside the mapped region";
            else if (buffer->peBuffer)
            {
                uint8_t *data = static_cast<uint8_t *>(buffer->peBuffer->Data());
                result = data ? data + offset : nullptr;
            }
        }

        if (errorText)
            ReportBufferValidationError(buffer, errorText);
        return result;
    }

    void const *wgpuBufferGetConstMappedRange(WGPUBuffer buffer, size_t offset, size_t size)
    {
        return wgpuBufferGetMappedRange(buffer, offset, size);
    }

    WGPUStatus wgpuBufferReadMappedRange(WGPUBuffer buffer, size_t offset, void *data, size_t size)
    {
        if (!buffer || !data)
            return WGPUStatus_Error;

        const char *errorText = nullptr;
        WGPUStatus status = WGPUStatus_Error;
        {
            std::lock_guard<std::mutex> lock(buffer->mapMutex);
            if (buffer->destroyed || buffer->mapState != WGPUBufferMapState_Mapped || !buffer->peBuffer)
            { /* status stays Error, no device-scope report needed */
            }
            else if ((buffer->mappedMode & WGPUMapMode_Read) == 0)
                errorText = "readMappedRange requires MAP_READ mapping";
            else if ((offset % 8) != 0 || (size % 4) != 0)
            { /* alignment error — silent */
            }
            else if (offset < buffer->mappedOffset ||
                     offset + size > buffer->mappedOffset + buffer->mappedSize)
            { /* range error — silent */
            }
            else
            {
                const uint8_t *src = static_cast<const uint8_t *>(buffer->peBuffer->Data());
                if (src)
                {
                    std::memcpy(data, src + offset, size);
                    status = WGPUStatus_Success;
                }
            }
        }

        if (errorText)
            ReportBufferValidationError(buffer, errorText);
        return status;
    }

    WGPUStatus wgpuBufferWriteMappedRange(WGPUBuffer buffer, size_t offset, void const *data, size_t size)
    {
        if (!buffer || !data)
            return WGPUStatus_Error;

        const char *errorText = nullptr;
        WGPUStatus status = WGPUStatus_Error;
        {
            std::lock_guard<std::mutex> lock(buffer->mapMutex);
            if (buffer->destroyed || buffer->mapState != WGPUBufferMapState_Mapped || !buffer->peBuffer)
            { /* status stays Error, no device-scope report needed */
            }
            else if ((buffer->mappedMode & WGPUMapMode_Write) == 0)
                errorText = "writeMappedRange requires MAP_WRITE mapping";
            else if ((offset % 8) != 0 || (size % 4) != 0)
            { /* alignment error — silent */
            }
            else if (offset < buffer->mappedOffset ||
                     offset + size > buffer->mappedOffset + buffer->mappedSize)
            { /* range error — silent */
            }
            else
            {
                uint8_t *dst = static_cast<uint8_t *>(buffer->peBuffer->Data());
                if (dst)
                {
                    std::memcpy(dst + offset, data, size);
                    status = WGPUStatus_Success;
                }
            }
        }

        if (errorText)
            ReportBufferValidationError(buffer, errorText);
        return status;
    }

    WGPUFuture wgpuBufferMapAsync(WGPUBuffer buffer,
                                  WGPUMapMode mode,
                                  size_t offset,
                                  size_t size,
                                  WGPUBufferMapCallbackInfo callbackInfo)
    {
        WGPUInstanceImpl *inst = (buffer && buffer->device) ? buffer->device->instance : nullptr;

        auto trackMapError = [&](const char *msg) -> WGPUFuture
        {
            if (!inst || !callbackInfo.callback)
                return inst ? inst->futures.NextId() : WGPUFuture{0};
            auto cb = callbackInfo.callback;
            auto u1 = callbackInfo.userdata1;
            auto u2 = callbackInfo.userdata2;
            std::string captured(msg);
            return inst->futures.TrackEvent(callbackInfo.mode,
                                            [cb, u1, u2, captured]()
                                            {
                                                WGPUStringView sv{captured.c_str(), captured.size()};
                                                cb(WGPUMapAsyncStatus_Error, sv, u1, u2);
                                            });
        };

        if (!buffer)
            return trackMapError("Null buffer");

        uint64_t rangeSize = 0;
        const bool rangeOk = ResolveRange(buffer->size, offset, size, rangeSize);

        const char *errorText = nullptr;
        {
            std::lock_guard<std::mutex> lock(buffer->mapMutex);

            if (buffer->destroyed)
                errorText = "mapAsync on destroyed buffer";
            else if (buffer->invalid)
                errorText = "mapAsync on invalid buffer";
            else if (buffer->mapState != WGPUBufferMapState_Unmapped)
                errorText = "mapAsync requires buffer to be unmapped";
            else if (!rangeOk)
                errorText = "mapAsync offset exceeds buffer size";
            else if ((offset % 8) != 0)
                errorText = "mapAsync offset must be a multiple of 8";
            else if ((rangeSize % 4) != 0)
                errorText = "mapAsync size must be a multiple of 4";
            else if (offset + rangeSize > buffer->size)
                errorText = "mapAsync range out of bounds";
            else if (mode == WGPUMapMode_None || (mode & ~(WGPUMapMode_Read | WGPUMapMode_Write)) != 0)
                errorText = "mapAsync mode must be exactly READ or WRITE";
            else if ((mode & WGPUMapMode_Read) && (mode & WGPUMapMode_Write))
                errorText = "mapAsync mode must not combine READ and WRITE";
            else if ((mode & WGPUMapMode_Read) && (buffer->usage & WGPUBufferUsage_MapRead) == 0)
                errorText = "mapAsync READ requires MAP_READ usage";
            else if ((mode & WGPUMapMode_Write) && (buffer->usage & WGPUBufferUsage_MapWrite) == 0)
                errorText = "mapAsync WRITE requires MAP_WRITE usage";

            if (!errorText)
            {
                buffer->mapState = WGPUBufferMapState_Pending;
                buffer->pendingCallback = callbackInfo;
                buffer->pendingOffset = offset;
                buffer->pendingSize = rangeSize;
                buffer->pendingMode = mode;
            }
        }

        if (errorText)
        {
            ReportBufferValidationError(buffer, errorText);
            return trackMapError(errorText);
        }

        if (!inst || !callbackInfo.callback)
            return inst ? inst->futures.NextId() : WGPUFuture{0};

        auto cb = callbackInfo.callback;
        auto u1 = callbackInfo.userdata1;
        auto u2 = callbackInfo.userdata2;
        WGPUQueueImpl *q = buffer->device ? buffer->device->queue : nullptr;
        uint64_t serial = buffer->lastUsageSerial.load(std::memory_order_acquire);

        // State transitions into Mapped inside the callback, after GPU work completes.
        return inst->futures.TrackEvent(callbackInfo.mode, [cb, u1, u2, buffer]()
                                        {
                                            {
                                                std::lock_guard<std::mutex> lock(buffer->mapMutex);
                                                if (buffer->mapState == WGPUBufferMapState_Pending)
                                                {
                                                    buffer->mapState = WGPUBufferMapState_Mapped;
                                                    buffer->mappedOffset = buffer->pendingOffset;
                                                    buffer->mappedSize = buffer->pendingSize;
                                                    buffer->mappedMode = buffer->pendingMode;
                                                    buffer->pendingCallback = {};
                                                    buffer->pendingOffset = 0;
                                                    buffer->pendingSize = 0;
                                                    buffer->pendingMode = WGPUMapMode_None;
                                                }
                                            }
                                            cb(WGPUMapAsyncStatus_Success, {nullptr, 0}, u1, u2); }, q, serial);
    }

    void wgpuBufferUnmap(WGPUBuffer buffer)
    {
        if (!buffer)
            return;

        WGPUBufferMapCallbackInfo pendingCallback{};
        bool hadPending = false;
        bool shouldFlush = false;
        {
            std::lock_guard<std::mutex> lock(buffer->mapMutex);
            if (buffer->destroyed)
                return;

            if (buffer->mapState == WGPUBufferMapState_Pending)
            {
                pendingCallback = buffer->pendingCallback;
                hadPending = true;
                buffer->pendingCallback = {};
                buffer->pendingOffset = 0;
                buffer->pendingSize = 0;
                buffer->pendingMode = WGPUMapMode_None;
                buffer->mapState = WGPUBufferMapState_Unmapped;
            }
            else if (buffer->mapState == WGPUBufferMapState_Mapped)
            {
                if ((buffer->mappedMode & WGPUMapMode_Write) && buffer->peBuffer)
                    shouldFlush = true;
                buffer->mapState = WGPUBufferMapState_Unmapped;
                buffer->mappedOffset = 0;
                buffer->mappedSize = 0;
                buffer->mappedMode = WGPUMapMode_None;
            }

            if (shouldFlush)
                buffer->peBuffer->Flush();
        }

        if (hadPending)
            FireMapCallback(pendingCallback, WGPUMapAsyncStatus_Aborted, "Buffer unmapped");
    }

    void wgpuBufferSetLabel(WGPUBuffer buffer, WGPUStringView label)
    {
        if (buffer)
            buffer->label = pwgpu::ToString(label);
    }

} // extern "C"
