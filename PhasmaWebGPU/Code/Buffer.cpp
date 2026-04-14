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
            if (buffer->peBuffer &&
                buffer->internalState != BufferInternalState::Destroyed)
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

        {
            std::lock_guard<std::mutex> lock(buffer->stateMutex);
            if (buffer->internalState == BufferInternalState::Destroyed)
                return;
            buffer->internalState = BufferInternalState::Destroyed;
        }

        wgpuBufferUnmap(buffer);

        {
            std::lock_guard<std::mutex> lock(buffer->stateMutex);
            if (buffer->peBuffer)
            {
                if (buffer->hostVisible)
                    buffer->peBuffer->Unmap();
                pe::Buffer::Destroy(buffer->peBuffer);
                buffer->peBuffer = nullptr;
            }
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
        if (!buffer)
            return WGPUBufferMapState_Unmapped;
        std::lock_guard<std::mutex> lock(buffer->stateMutex);
        return buffer->mapState;
    }

    void *wgpuBufferGetMappedRange(WGPUBuffer buffer, size_t offset, size_t size)
    {
        if (!buffer)
            return nullptr;

        const char *errorText = nullptr;
        void *result = nullptr;
        {
            std::lock_guard<std::mutex> lock(buffer->stateMutex);
            uint64_t rangeSize = 0;

            if (buffer->mapState != WGPUBufferMapState_Mapped)
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
            else
            {
                // Check overlap with previously returned sub-ranges
                bool overlaps = false;
                for (const auto &r : buffer->mappedSubRanges)
                {
                    if (offset < r.second && offset + rangeSize > r.first)
                    {
                        overlaps = true;
                        break;
                    }
                }
                if (overlaps)
                    errorText = "getMappedRange overlaps a previously returned range";
                else
                {
                    uint8_t *data = nullptr;
                    if (buffer->peBuffer)
                        data = static_cast<uint8_t *>(buffer->peBuffer->Data());
                    else if (!buffer->shadowData.empty())
                        data = buffer->shadowData.data();
                    result = data ? data + offset : nullptr;
                    if (result)
                        buffer->mappedSubRanges.push_back({offset, offset + rangeSize});
                }
            }
        }

        // Per spec, getMappedRange surfaces failures as a synchronous throw in the
        // binding (null return) rather than a device-scope validation error.
        (void)errorText;
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
            std::lock_guard<std::mutex> lock(buffer->stateMutex);
            if (buffer->mapState != WGPUBufferMapState_Mapped || !buffer->peBuffer)
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
            std::lock_guard<std::mutex> lock(buffer->stateMutex);
            if (buffer->mapState != WGPUBufferMapState_Mapped || !buffer->peBuffer)
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

        auto trackMapErrorDeferred = [&](const char *msg) -> WGPUFuture
        {
            if (!inst || !callbackInfo.callback)
                return inst ? inst->futures.NextId() : WGPUFuture{0};
            auto cb = callbackInfo.callback;
            auto u1 = callbackInfo.userdata1;
            auto u2 = callbackInfo.userdata2;
            std::string captured(msg);
            WGPUCallbackMode deferredMode = callbackInfo.mode;
            if (deferredMode == WGPUCallbackMode_AllowSpontaneous)
                deferredMode = WGPUCallbackMode_AllowProcessEvents;
            return inst->futures.TrackEvent(
                deferredMode,
                [cb, u1, u2, captured, buffer]()
                {
                    {
                        std::lock_guard<std::mutex> lock(buffer->stateMutex);
                        if (buffer->mapState != WGPUBufferMapState_Pending ||
                            buffer->pendingCallback.userdata1 != u1)
                            return;
                        buffer->mapState = WGPUBufferMapState_Unmapped;
                        buffer->pendingCallback = {};
                        buffer->pendingOffset = 0;
                        buffer->pendingSize = 0;
                        buffer->pendingMode = WGPUMapMode_None;
                        if (!buffer->invalid &&
                            buffer->internalState != BufferInternalState::Destroyed)
                            buffer->internalState = BufferInternalState::Available;
                    }
                    WGPUStringView sv{captured.c_str(), captured.size()};
                    cb(WGPUMapAsyncStatus_Error, sv, u1, u2);
                });
        };

        auto fireMapErrorNow = [&](const char *msg) -> WGPUFuture
        {
            WGPUFuture future = inst ? inst->futures.NextId() : WGPUFuture{0};
            if (callbackInfo.callback)
            {
                WGPUStringView sv = pwgpu::ToStringView(msg);
                callbackInfo.callback(WGPUMapAsyncStatus_Error, sv,
                                      callbackInfo.userdata1, callbackInfo.userdata2);
            }
            return future;
        };

        if (!buffer)
            return fireMapErrorNow("Null buffer");

        uint64_t rangeSize = 0;
        const bool rangeOk = ResolveRange(buffer->size, offset, size, rangeSize);

        const char *errorText = nullptr;
        bool earlyReject = false;
        {
            std::lock_guard<std::mutex> lock(buffer->stateMutex);

            if (buffer->mapState != WGPUBufferMapState_Unmapped)
            {
                errorText = "mapAsync requires buffer to be unmapped";
                earlyReject = true;
            }

            if (!errorText)
            {
                if (buffer->invalid)
                    errorText = "mapAsync on invalid buffer";
                else if (buffer->internalState != BufferInternalState::Available)
                    errorText = "mapAsync requires internal state 'available'";
                else if (!rangeOk)
                    errorText = "mapAsync offset exceeds buffer size";
                else if ((offset % 8) != 0)
                    errorText = "mapAsync offset must be a multiple of 8";
                else if ((rangeSize % 4) != 0)
                    errorText = "mapAsync size must be a multiple of 4";
                else if (offset + rangeSize > buffer->size)
                    errorText = "mapAsync range out of bounds";
                else if (mode == WGPUMapMode_None ||
                         (mode & ~(WGPUMapMode_Read | WGPUMapMode_Write)) != 0)
                    errorText = "mapAsync mode must be exactly READ or WRITE";
                else if ((mode & WGPUMapMode_Read) && (mode & WGPUMapMode_Write))
                    errorText = "mapAsync mode must not combine READ and WRITE";
                else if ((mode & WGPUMapMode_Read) &&
                         (buffer->usage & WGPUBufferUsage_MapRead) == 0)
                    errorText = "mapAsync READ requires MAP_READ usage";
                else if ((mode & WGPUMapMode_Write) &&
                         (buffer->usage & WGPUBufferUsage_MapWrite) == 0)
                    errorText = "mapAsync WRITE requires MAP_WRITE usage";
            }

            if (!earlyReject)
            {
                buffer->mapState = WGPUBufferMapState_Pending;
                buffer->internalState = BufferInternalState::Unavailable;
                buffer->pendingCallback = callbackInfo;
                buffer->pendingOffset = offset;
                buffer->pendingSize = errorText ? 0 : rangeSize;
                buffer->pendingMode = errorText ? WGPUMapMode_None : mode;
            }
        }

        if (errorText)
        {
            ReportBufferValidationError(buffer, errorText);
            if (earlyReject)
                return fireMapErrorNow(errorText);
            else
                return trackMapErrorDeferred(errorText);
        }

        if (!inst || !callbackInfo.callback)
            return inst ? inst->futures.NextId() : WGPUFuture{0};

        auto cb = callbackInfo.callback;
        auto u1 = callbackInfo.userdata1;
        auto u2 = callbackInfo.userdata2;
        WGPUQueueImpl *q = buffer->device ? buffer->device->queue : nullptr;
        uint64_t serial = buffer->lastUsageSerial.load(std::memory_order_acquire);

        WGPUCallbackMode successMode = callbackInfo.mode;
        if (successMode == WGPUCallbackMode_AllowSpontaneous)
            successMode = WGPUCallbackMode_AllowProcessEvents;

        return inst->futures.TrackEvent(
            successMode,
            [cb, u1, u2, buffer]()
            {
                WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
                const char *message = nullptr;
                {
                    std::lock_guard<std::mutex> lock(buffer->stateMutex);
                    if (buffer->mapState != WGPUBufferMapState_Pending)
                    {
                        return;
                    }
                    if (buffer->pendingCallback.userdata1 != u1)
                    {
                        return;
                    }

                    if (buffer->internalState == BufferInternalState::Destroyed)
                    {
                        buffer->mapState = WGPUBufferMapState_Unmapped;
                        buffer->pendingCallback = {};
                        buffer->pendingOffset = 0;
                        buffer->pendingSize = 0;
                        buffer->pendingMode = WGPUMapMode_None;
                        status = WGPUMapAsyncStatus_Aborted;
                        message = "Buffer was destroyed before map completed";
                    }
                    else
                    {
                        buffer->mapState = WGPUBufferMapState_Mapped;
                        buffer->mappedOffset = buffer->pendingOffset;
                        buffer->mappedSize = buffer->pendingSize;
                        buffer->mappedMode = buffer->pendingMode;
                        buffer->pendingCallback = {};
                        buffer->pendingOffset = 0;
                        buffer->pendingSize = 0;
                        buffer->pendingMode = WGPUMapMode_None;
                        status = WGPUMapAsyncStatus_Success;
                    }
                }
                cb(status, pwgpu::ToStringView(message), u1, u2);
            },
            q, serial);
    }

    void wgpuBufferUnmap(WGPUBuffer buffer)
    {
        if (!buffer)
            return;

        WGPUBufferMapCallbackInfo pendingCallback{};
        bool hadPending = false;
        bool shouldFlush = false;
        {
            std::lock_guard<std::mutex> lock(buffer->stateMutex);

            if (buffer->mapState == WGPUBufferMapState_Pending)
            {
                pendingCallback = buffer->pendingCallback;
                hadPending = true;
                buffer->pendingCallback = {};
                buffer->pendingOffset = 0;
                buffer->pendingSize = 0;
                buffer->pendingMode = WGPUMapMode_None;
                buffer->mapState = WGPUBufferMapState_Unmapped;
                buffer->mappedSubRanges.clear();
            }
            else if (buffer->mapState == WGPUBufferMapState_Mapped)
            {
                if ((buffer->mappedMode & WGPUMapMode_Write) && buffer->peBuffer)
                    shouldFlush = true;
                buffer->mapState = WGPUBufferMapState_Unmapped;
                buffer->mappedOffset = 0;
                buffer->mappedSize = 0;
                buffer->mappedMode = WGPUMapMode_None;
                buffer->mappedSubRanges.clear();
            }
            else
            {
                return;
            }

            if (shouldFlush)
                buffer->peBuffer->Flush();

            if (!buffer->invalid &&
                buffer->internalState != BufferInternalState::Destroyed)
                buffer->internalState = BufferInternalState::Available;
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
