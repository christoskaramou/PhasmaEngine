#include "Device.h"
#include "Buffer.h"
#include "Texture.h"
#include "CommandEncoder.h"
#include "FormatMap.h"
#include "Instance.h"
#include "Utils.h"
#include "API/Command.h"
#include "API/Queue.h"
#include "API/Semaphore.h"
#include "API/Image.h"
#include "API/Buffer.h"
#include "API/RHI.h"
#include "API/StagingManager.h"

pe::Semaphore *WGPUQueueImpl::GetSemaphore() const
{
    return peQueue ? peQueue->GetSubmissionsSemaphore() : nullptr;
}

void WGPUQueueImpl::RecyclePendingSubmits()
{
    pe::Semaphore *sem = GetSemaphore();
    if (!sem)
        return;
    const uint64_t completedSerial = sem->GetValue();

    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        auto it = pendingSubmits.begin();
        while (it != pendingSubmits.end())
        {
            if (completedSerial >= it->serial)
            {
                it->cmd->Wait();
                it->cmd->Return();
                it = pendingSubmits.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    if (auto *sm = pe::RHII.GetStagingManager())
        sm->RemoveUnused();
}

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

    // ---- §19.1 submit() ----

    void wgpuQueueSubmit(WGPUQueue queue, size_t commandCount, WGPUCommandBuffer const *commands)
    {
        if (!queue || !queue->peQueue)
            return;
        if (!commandCount || !commands)
            return;

        queue->RecyclePendingSubmits();
        if (queue->device)
            queue->device->ReclaimCompletedTextureDeletions();

        std::vector<pe::CommandBuffer *> cmds;
        std::vector<WGPUCommandBuffer> validCBs;
        cmds.reserve(commandCount);
        validCBs.reserve(commandCount);

        for (size_t i = 0; i < commandCount; ++i)
        {
            WGPUCommandBuffer cb = commands[i];
            if (!cb || !cb->cmd || cb->submitted)
            {
                if (queue->device)
                {
                    queue->device->reportError(
                        WGPUErrorType_Validation,
                        pwgpu::ToStringView("Submit: command buffer is null, already submitted, or invalid"));
                }
                continue;
            }

            if (cb->invalid)
            {
                if (queue->device)
                {
                    queue->device->reportError(
                        WGPUErrorType_Validation,
                        pwgpu::ToStringView("Submit: command buffer is invalid"));
                }
                continue;
            }

            bool hasUnavailable = false;
            for (auto *buf : cb->retained.usedBuffers)
            {
                if (!buf)
                    continue;
                std::lock_guard<std::mutex> lock(buf->stateMutex);
                if (buf->internalState != BufferInternalState::Available)
                {
                    hasUnavailable = true;
                    if (queue->device)
                        queue->device->reportError(
                            WGPUErrorType_Validation,
                            pwgpu::ToStringView(
                                "Submit: buffer [[internal state]] is not 'available'"));
                    break;
                }
            }
            if (hasUnavailable)
                continue;

            bool textureDestroyed = false;
            for (auto *tv : cb->retained.textureViews)
            {
                if (!tv || !tv->texture)
                    continue;
                if (tv->texture->destroyed || tv->texture->invalid)
                {
                    textureDestroyed = true;
                    if (queue->device)
                        queue->device->reportError(
                            WGPUErrorType_Validation,
                            pwgpu::ToStringView(
                                "Submit: command buffer references a destroyed texture"));
                    break;
                }
            }
            if (textureDestroyed)
                continue;

            for (auto *tex : cb->retained.usedTextures)
            {
                if (!tex)
                    continue;
                if (tex->destroyed || tex->invalid)
                {
                    textureDestroyed = true;
                    if (queue->device)
                        queue->device->reportError(
                            WGPUErrorType_Validation,
                            pwgpu::ToStringView(
                                "Submit: command buffer references a destroyed texture"));
                    break;
                }
            }
            if (textureDestroyed)
                continue;

            cb->submitted = true;
            cmds.push_back(cb->cmd);
            validCBs.push_back(cb);
        }

        if (cmds.empty())
            return;

        queue->peQueue->Submit(static_cast<uint32_t>(cmds.size()), cmds.data(), nullptr, nullptr);
        const uint64_t serial = queue->peQueue->GetSubmissionCount();

        {
            std::lock_guard<std::mutex> lock(queue->pendingMutex);
            for (auto *cmd : cmds)
                queue->pendingSubmits.push_back({cmd, serial});
        }

        queue->lastSubmissionSerial.store(serial, std::memory_order_release);

        for (auto *cb : validCBs)
        {
            for (auto *buf : cb->retained.usedBuffers)
            {
                if (buf)
                    buf->lastUsageSerial.store(serial, std::memory_order_release);
            }
            for (auto *tv : cb->retained.textureViews)
            {
                if (tv && tv->texture)
                    tv->texture->lastUsageSerial.store(serial, std::memory_order_release);
            }
            for (auto *tex : cb->retained.usedTextures)
            {
                if (tex)
                    tex->lastUsageSerial.store(serial, std::memory_order_release);
            }
            cb->cmd = nullptr;
        }
    }

    // ---- §19.2 writeBuffer (immediate) ----

    void wgpuQueueWriteBuffer(WGPUQueue queue, WGPUBuffer buffer,
                              uint64_t bufferOffset, void const *data, size_t size)
    {
        if (!buffer)
            return;
        if (queue)
        {
            queue->RecyclePendingSubmits();
            if (queue->device)
                queue->device->ReclaimCompletedTextureDeletions();
        }

        WGPUDeviceImpl *reportDevice =
            (queue && queue->device) ? queue->device : buffer->device;
        auto reportValidation = [&](const char *msg)
        {
            if (reportDevice)
                reportDevice->reportError(WGPUErrorType_Validation,
                                          pwgpu::ToStringView(std::string("writeBuffer: ") + msg));
        };

        if (!data && size > 0)
        {
            reportValidation("data is null");
            return;
        }
        if (buffer->invalid)
        {
            reportValidation("buffer is invalid");
            return;
        }
        if (queue && buffer->device != queue->device)
        {
            reportValidation("buffer belongs to a different device");
            return;
        }
        if (buffer->internalState.load(std::memory_order_acquire) != BufferInternalState::Available)
        {
            reportValidation("buffer is destroyed or mapped");
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_CopyDst))
        {
            reportValidation("buffer usage must include COPY_DST");
            return;
        }
        if (bufferOffset % 4 != 0)
        {
            reportValidation("bufferOffset must be multiple of 4");
            return;
        }
        if (size % 4 != 0)
        {
            reportValidation("size must be multiple of 4");
            return;
        }
        if (bufferOffset > buffer->size || size > buffer->size - bufferOffset)
        {
            reportValidation("bufferOffset + size exceeds buffer size");
            return;
        }

        pe::Buffer *backing = nullptr;
        {
            std::lock_guard<std::mutex> lock(buffer->stateMutex);
            if (buffer->internalState != BufferInternalState::Available ||
                !buffer->peBuffer)
            {
                reportValidation("buffer state changed during writeBuffer");
                return;
            }
            backing = buffer->peBuffer;
        }

        if (buffer->hostVisible)
        {
            pe::BufferRange range{const_cast<void *>(data), size, static_cast<size_t>(bufferOffset)};
            backing->Copy(1, &range, false);
        }
        else if (queue && queue->peQueue)
        {
            pe::CommandBuffer *cmd = queue->peQueue->AcquireCommandBuffer();
            cmd->Begin();
            cmd->CopyBufferStaged(backing, const_cast<void *>(data), size,
                                  static_cast<size_t>(bufferOffset));
            cmd->End();
            queue->peQueue->Submit(1, &cmd, nullptr, nullptr);

            const uint64_t serial = queue->peQueue->GetSubmissionCount();
            {
                std::lock_guard<std::mutex> lock(queue->pendingMutex);
                queue->pendingSubmits.push_back({cmd, serial});
            }
            queue->lastSubmissionSerial.store(serial, std::memory_order_release);
            buffer->lastUsageSerial.store(serial, std::memory_order_release);
        }
    }

    static bool IsDSWriteTextureAspectSupported(WGPUTextureFormat fmt, WGPUTextureAspect aspect)
    {
        bool hasD = pwgpu::HasDepthAspect(fmt);
        bool hasS = pwgpu::HasStencilAspect(fmt);
        if (!hasD && !hasS)
            return true;
        bool all = (aspect == WGPUTextureAspect_All);
        bool depth = (aspect == WGPUTextureAspect_DepthOnly);
        bool sten = (aspect == WGPUTextureAspect_StencilOnly);
        switch (fmt)
        {
        case WGPUTextureFormat_Stencil8:
            return all || sten;
        case WGPUTextureFormat_Depth16Unorm:
            return all || depth;
        case WGPUTextureFormat_Depth32Float:
            return false;
        case WGPUTextureFormat_Depth24Plus:
            return false;
        case WGPUTextureFormat_Depth24PlusStencil8:
            return sten;
        case WGPUTextureFormat_Depth32FloatStencil8:
            return sten;
        default:
            return true;
        }
    }

    // ---- §19.3 writeTexture (immediate) ----

    void wgpuQueueWriteTexture(WGPUQueue queue,
                               WGPUTexelCopyTextureInfo const *destination,
                               void const *data,
                               size_t dataSize,
                               WGPUTexelCopyBufferLayout const *dataLayout,
                               WGPUExtent3D const *writeSize)
    {
        if (!queue || !queue->peQueue)
            return;
        queue->RecyclePendingSubmits();
        if (queue->device)
            queue->device->ReclaimCompletedTextureDeletions();
        auto fail = [&]()
        {
            if (queue->device)
                queue->device->reportError(WGPUErrorType_Validation,
                                           pwgpu::ToStringView("Queue.writeTexture(): validation failed"));
        };
        if (!destination || !destination->texture || !dataLayout || !writeSize)
        {
            fail();
            return;
        }
        if (!destination->texture->image || destination->texture->destroyed || destination->texture->invalid)
        {
            fail();
            return;
        }
        if (destination->texture->device != queue->device)
        {
            fail();
            return;
        }
        if (destination->mipLevel >= destination->texture->mipLevelCount)
        {
            fail();
            return;
        }
        if (destination->texture->sampleCount > 1)
        {
            fail();
            return;
        }
        if (!IsDSWriteTextureAspectSupported(destination->texture->format, destination->aspect))
        {
            fail();
            return;
        }
        {
            WGPUTextureImpl *tex = destination->texture;
            uint32_t mipW = std::max(1u, tex->size.width >> destination->mipLevel);
            uint32_t mipH = std::max(1u, tex->size.height >> destination->mipLevel);
            uint32_t mipD = (tex->dimension == WGPUTextureDimension_3D)
                                ? std::max(1u, tex->size.depthOrArrayLayers >> destination->mipLevel)
                                : tex->size.depthOrArrayLayers;
            uint64_t ox = destination->origin.x;
            uint64_t oy = destination->origin.y;
            uint64_t oz = destination->origin.z;
            if (ox + writeSize->width > mipW ||
                oy + writeSize->height > mipH ||
                oz + writeSize->depthOrArrayLayers > mipD)
            {
                fail();
                return;
            }
            if (pwgpu::HasDepthAspect(tex->format) || pwgpu::HasStencilAspect(tex->format))
            {
                if (ox != 0 || oy != 0 ||
                    writeSize->width != mipW || writeSize->height != mipH)
                {
                    fail();
                    return;
                }
            }
            uint32_t bw, bh;
            pwgpu::GetTexelBlockSize(tex->format, bw, bh);
            if (bw > 1 || bh > 1)
            {
                if (ox % bw != 0 || oy % bh != 0)
                {
                    fail();
                    return;
                }
                if (ox + writeSize->width != mipW && writeSize->width % bw != 0)
                {
                    fail();
                    return;
                }
                if (oy + writeSize->height != mipH && writeSize->height % bh != 0)
                {
                    fail();
                    return;
                }
            }
        }
        if (!(destination->texture->usage & WGPUTextureUsage_CopyDst))
        {
            fail();
            return;
        }

        pe::Image *image = destination->texture->image;
        WGPUTextureFormat fmt = destination->texture->format;
        bool is3D = (destination->texture->dimension == WGPUTextureDimension_3D);

        uint32_t blockW, blockH;
        pwgpu::GetTexelBlockSize(fmt, blockW, blockH);
        uint32_t footprint = pwgpu::TexelBlockCopyFootprint(fmt, destination->aspect);
        if (footprint == 0)
        {
            fail();
            return;
        }

        {
            uint32_t widthInBlocks = (writeSize->width + blockW - 1) / blockW;
            uint32_t heightInBlocks = (writeSize->height + blockH - 1) / blockH;
            uint32_t minBytesPerRow = widthInBlocks * footprint;
            uint32_t depth = writeSize->depthOrArrayLayers;
            bool bprProvided = (dataLayout->bytesPerRow != WGPU_COPY_STRIDE_UNDEFINED);
            bool rpiProvided = (dataLayout->rowsPerImage != WGPU_COPY_STRIDE_UNDEFINED);
            bool bytesPerRowRequired = (heightInBlocks > 1) || (depth > 1);
            if (bytesPerRowRequired && !bprProvided)
            {
                fail();
                return;
            }
            if (bprProvided && dataLayout->bytesPerRow < minBytesPerRow)
            {
                fail();
                return;
            }
            if (depth > 1 && !rpiProvided)
            {
                fail();
                return;
            }
            if (rpiProvided && dataLayout->rowsPerImage < heightInBlocks)
            {
                fail();
                return;
            }
            uint64_t bpr = bprProvided ? dataLayout->bytesPerRow : minBytesPerRow;
            uint64_t rpi = rpiProvided ? dataLayout->rowsPerImage : heightInBlocks;
            uint64_t bytesPerImage = bpr * rpi;
            uint64_t bytesInLastImage = (heightInBlocks > 0)
                                            ? bpr * (heightInBlocks - 1) + minBytesPerRow
                                            : 0;
            uint64_t requiredBytesInCopy = 0;
            if (depth > 1)
                requiredBytesInCopy += bytesPerImage * (depth - 1);
            if (depth > 0)
                requiredBytesInCopy += bytesInLastImage;
            uint64_t required = dataLayout->offset + requiredBytesInCopy;
            if (required > dataSize)
            {
                fail();
                return;
            }
        }

        if (writeSize->width == 0 || writeSize->height == 0 || writeSize->depthOrArrayLayers == 0)
            return;

        vk::ImageAspectFlags vkAspect = pwgpu::ToVkAspect(destination->aspect, fmt);

        pe::CommandBuffer *cmd = queue->peQueue->AcquireCommandBuffer();
        cmd->Begin();

        pe::ImageBarrierInfo barrier{};
        barrier.image = image;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barrier.accessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.layout = vk::ImageLayout::eTransferDstOptimal;
        barrier.baseMipLevel = destination->mipLevel;
        barrier.mipLevels = 1;
        barrier.baseArrayLayer = is3D ? 0 : destination->origin.z;
        barrier.arrayLayers = is3D ? 1 : writeSize->depthOrArrayLayers;
        cmd->ImageBarrier(barrier);

        pe::StagingAllocation alloc = pe::RHII.GetStagingManager()->Allocate(dataSize);
        std::memcpy(alloc.data, data, dataSize);
        alloc.buffer->Flush(dataSize, 0);

        vk::BufferImageCopy2 region{};
        region.bufferOffset = dataLayout->offset;
        region.bufferRowLength = (footprint > 0 && dataLayout->bytesPerRow != WGPU_COPY_STRIDE_UNDEFINED)
                                     ? (dataLayout->bytesPerRow / footprint) * blockW
                                     : 0;
        region.bufferImageHeight = (dataLayout->rowsPerImage != WGPU_COPY_STRIDE_UNDEFINED)
                                       ? dataLayout->rowsPerImage * blockH
                                       : 0;
        region.imageSubresource.aspectMask = vkAspect;
        region.imageSubresource.mipLevel = destination->mipLevel;
        region.imageSubresource.baseArrayLayer = is3D ? 0 : destination->origin.z;
        region.imageSubresource.layerCount = is3D ? 1 : writeSize->depthOrArrayLayers;
        region.imageOffset = vk::Offset3D{static_cast<int32_t>(destination->origin.x),
                                          static_cast<int32_t>(destination->origin.y),
                                          is3D ? static_cast<int32_t>(destination->origin.z) : 0};
        region.imageExtent = vk::Extent3D{writeSize->width, writeSize->height,
                                          is3D ? writeSize->depthOrArrayLayers : 1};

        vk::CopyBufferToImageInfo2 copyInfo{};
        copyInfo.srcBuffer = alloc.buffer->ApiHandle();
        copyInfo.dstImage = image->ApiHandle();
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        cmd->ApiHandle().copyBufferToImage2(copyInfo);

        cmd->AddAfterWaitCallback([alloc = std::move(alloc)]() mutable
                                  { pe::RHII.GetStagingManager()->SetUnused(alloc); });

        cmd->End();
        queue->peQueue->Submit(1, &cmd, nullptr, nullptr);

        const uint64_t serial = queue->peQueue->GetSubmissionCount();
        {
            std::lock_guard<std::mutex> lock(queue->pendingMutex);
            queue->pendingSubmits.push_back({cmd, serial});
        }
        queue->lastSubmissionSerial.store(serial, std::memory_order_release);
        destination->texture->lastUsageSerial.store(serial, std::memory_order_release);
    }

    WGPUFuture wgpuQueueOnSubmittedWorkDone(WGPUQueue queue, WGPUQueueWorkDoneCallbackInfo callbackInfo)
    {
        WGPUInstanceImpl *inst = (queue && queue->device) ? queue->device->instance : nullptr;
        if (!inst || !callbackInfo.callback)
            return inst ? inst->futures.NextId() : WGPUFuture{0};
        auto cb = callbackInfo.callback;
        auto u1 = callbackInfo.userdata1;
        auto u2 = callbackInfo.userdata2;
        uint64_t serial = queue->lastSubmissionSerial.load(std::memory_order_acquire);
        return inst->futures.TrackEvent(callbackInfo.mode, [cb, u1, u2]()
                                        { cb(WGPUQueueWorkDoneStatus_Success, {nullptr, 0}, u1, u2); }, queue, serial);
    }

    void wgpuQueueSetLabel(WGPUQueue queue, WGPUStringView label)
    {
        if (queue)
            queue->label = pwgpu::ToString(label);
    }

} // extern "C"
