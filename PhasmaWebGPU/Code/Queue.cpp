#include "Device.h"
#include "Buffer.h"
#include "Texture.h"
#include "BindGroup.h"
#include "RenderBundle.h"
#include "CommandEncoder.h"
#include "FormatMap.h"
#include "Instance.h"
#include "QuerySet.h"
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

        // §22 device-lost: operations that send no message back skip their usual steps.
        // Mark CBs submitted so later release is safe, then no-op silently (no validation).
        if (queue->device && queue->device->destroyed)
        {
            for (size_t i = 0; i < commandCount; ++i)
                if (commands[i])
                    commands[i]->submitted = true;
            return;
        }

        queue->RecyclePendingSubmits();
        if (queue->device)
            queue->device->ReclaimCompletedTextureDeletions();

        std::vector<pe::CommandBuffer *> cmds;
        std::vector<WGPUCommandBuffer> validCBs;
        cmds.reserve(commandCount);
        validCBs.reserve(commandCount);

        bool submitInvalid = false;
        {
            std::unordered_set<WGPUCommandBuffer> seen;
            seen.reserve(commandCount);
            for (size_t i = 0; i < commandCount; ++i)
            {
                WGPUCommandBuffer cb = commands[i];
                if (!cb)
                    continue;
                if (!seen.insert(cb).second)
                {
                    if (queue->device)
                        queue->device->reportError(
                            WGPUErrorType_Validation,
                            pwgpu::ToStringView(
                                "Submit: commandBuffers must be unique (duplicate entry)"));
                    submitInvalid = true;
                    break;
                }
            }
        }

        for (size_t i = 0; i < commandCount; ++i)
        {
            WGPUCommandBuffer cb = commands[i];
            if (!cb)
            {
                if (queue->device)
                    queue->device->reportError(
                        WGPUErrorType_Validation,
                        pwgpu::ToStringView("Submit: command buffer is null"));
                submitInvalid = true;
                continue;
            }

            if (cb->submitted)
            {
                if (queue->device)
                    queue->device->reportError(
                        WGPUErrorType_Validation,
                        pwgpu::ToStringView("Submit: command buffer has already been submitted"));
                submitInvalid = true;
                continue;
            }

            if (cb->invalid)
            {
                if (queue->device)
                    queue->device->reportError(
                        WGPUErrorType_Validation,
                        pwgpu::ToStringView("Submit: command buffer is invalid"));
                submitInvalid = true;
                continue;
            }

            // §3.3: bind group references a resource destroyed before submit.
            if (cb->deferredResourceError)
            {
                if (queue->device)
                    queue->device->reportError(
                        WGPUErrorType_Validation,
                        pwgpu::ToStringView(
                            "Submit: command buffer references a destroyed resource"));
                submitInvalid = true;
                continue;
            }

            if (cb->device && queue->device && cb->device != queue->device)
            {
                queue->device->reportError(
                    WGPUErrorType_Validation,
                    pwgpu::ToStringView(
                        "Submit: command buffer belongs to a different device"));
                submitInvalid = true;
                continue;
            }

            if (!cb->cmd)
            {
                if (queue->device)
                    queue->device->reportError(
                        WGPUErrorType_Validation,
                        pwgpu::ToStringView("Submit: command buffer has no backing command list"));
                submitInvalid = true;
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
            {
                submitInvalid = true;
                continue;
            }

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
            {
                submitInvalid = true;
                continue;
            }

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
            {
                submitInvalid = true;
                continue;
            }

            auto scanBg = [&](WGPUBindGroupImpl *bg) -> bool
            {
                if (!bg)
                    return false;
                for (auto &use : bg->bufferUses)
                {
                    WGPUBufferImpl *buf = use.buffer;
                    if (!buf)
                        continue;
                    std::lock_guard<std::mutex> lock(buf->stateMutex);
                    if (buf->internalState != BufferInternalState::Available)
                    {
                        if (queue->device)
                            queue->device->reportError(
                                WGPUErrorType_Validation,
                                pwgpu::ToStringView(
                                    "Submit: bind group references a destroyed buffer"));
                        return true;
                    }
                }
                for (auto &use : bg->textureUses)
                {
                    if (!use.view || !use.view->texture)
                        continue;
                    if (use.view->texture->destroyed || use.view->texture->invalid)
                    {
                        if (queue->device)
                            queue->device->reportError(
                                WGPUErrorType_Validation,
                                pwgpu::ToStringView(
                                    "Submit: bind group references a destroyed texture"));
                        return true;
                    }
                }
                return false;
            };

            bool bgResourceBad = false;
            for (auto *bg : cb->retained.bindGroups)
            {
                if (scanBg(bg))
                {
                    bgResourceBad = true;
                    break;
                }
            }
            if (!bgResourceBad)
            {
                for (auto *rb : cb->retained.renderBundles)
                {
                    if (!rb)
                        continue;
                    for (auto *bg : rb->retainedBindGroups)
                    {
                        if (scanBg(bg))
                        {
                            bgResourceBad = true;
                            break;
                        }
                    }
                    if (bgResourceBad)
                        break;
                }
            }
            if (bgResourceBad)
            {
                submitInvalid = true;
                continue;
            }

            bool bundleBufferBad = false;
            for (auto *rb : cb->retained.renderBundles)
            {
                if (!rb)
                    continue;
                for (auto *buf : rb->retainedBuffers)
                {
                    if (!buf)
                        continue;
                    std::lock_guard<std::mutex> lock(buf->stateMutex);
                    if (buf->internalState != BufferInternalState::Available)
                    {
                        bundleBufferBad = true;
                        if (queue->device)
                            queue->device->reportError(
                                WGPUErrorType_Validation,
                                pwgpu::ToStringView(
                                    "Submit: render bundle references a destroyed buffer"));
                        break;
                    }
                }
                if (bundleBufferBad)
                    break;
            }
            if (bundleBufferBad)
            {
                submitInvalid = true;
                continue;
            }

            bool querySetBad = false;
            for (auto *qs : cb->retained.querySets)
            {
                if (qs && qs->destroyed)
                {
                    querySetBad = true;
                    if (queue->device)
                        queue->device->reportError(
                            WGPUErrorType_Validation,
                            pwgpu::ToStringView(
                                "Submit: command buffer references a destroyed query set"));
                    break;
                }
            }
            if (querySetBad)
            {
                submitInvalid = true;
                continue;
            }

            cmds.push_back(cb->cmd);
            validCBs.push_back(cb);
        }

        if (submitInvalid || cmds.empty())
        {
            // Submit failed — the cb will never execute, so return its backing
            // pe::CommandBuffer to the RHI pool before marking submitted. Without
            // this, wgpuCommandBufferRelease sees submitted=true and skips Return,
            // leaking the CB + its cached GpuTimers + Event at RHI shutdown.
            for (size_t i = 0; i < commandCount; ++i)
            {
                if (!commands[i])
                    continue;
                if (commands[i]->cmd)
                {
                    commands[i]->cmd->Return();
                    commands[i]->cmd = nullptr;
                }
                commands[i]->submitted = true;
            }
            return;
        }

        for (auto *cb : validCBs)
            cb->submitted = true;

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
            auto markBindGroupBuffers = [serial](WGPUBindGroupImpl *bg)
            {
                if (!bg)
                    return;
                for (auto &use : bg->bufferUses)
                {
                    if (use.buffer)
                        use.buffer->lastUsageSerial.store(serial, std::memory_order_release);
                }
            };

            for (auto *buf : cb->retained.usedBuffers)
            {
                if (buf)
                    buf->lastUsageSerial.store(serial, std::memory_order_release);
            }
            for (auto *bg : cb->retained.bindGroups)
                markBindGroupBuffers(bg);
            for (auto *rb : cb->retained.renderBundles)
            {
                if (!rb)
                    continue;
                for (auto *buf : rb->retainedBuffers)
                {
                    if (buf)
                        buf->lastUsageSerial.store(serial, std::memory_order_release);
                }
                for (auto *bg : rb->retainedBindGroups)
                    markBindGroupBuffers(bg);
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
        {
            BufferInternalState st = buffer->internalState.load(std::memory_order_acquire);
            if (st == BufferInternalState::Destroyed)
            {
                reportValidation("buffer is destroyed");
                return;
            }
            if (st != BufferInternalState::Available)
            {
                reportValidation("buffer internal state is not \"available\" (mapped or pending map)");
                return;
            }
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
            const uint64_t lastUsage = buffer->lastUsageSerial.load(std::memory_order_acquire);
            if (lastUsage != 0 && queue)
            {
                pe::Semaphore *sem = queue->GetSemaphore();
                if (sem && sem->GetValue() < lastUsage)
                    sem->WaitTimeout(lastUsage, UINT64_MAX);
            }

            pe::BufferRange range{const_cast<void *>(data), size, static_cast<size_t>(bufferOffset)};
            backing->Copy(1, &range, false);

            pe::BufferTrackInfo &trackInfo = backing->GetTrackInfo();
            trackInfo.stageMask = vk::PipelineStageFlagBits2::eHost;
            trackInfo.accessMask = vk::AccessFlagBits2::eHostWrite;
        }
        else if (queue && queue->peQueue)
        {
            pe::CommandBuffer *cmd = queue->peQueue->AcquireCommandBuffer();
            cmd->Begin();
            cmd->CopyBufferStaged(backing, const_cast<void *>(data), size,
                                  static_cast<size_t>(bufferOffset));

            if (size > 0)
            {
                vk::BufferMemoryBarrier2 bmb{};
                bmb.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
                bmb.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
                bmb.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
                bmb.dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
                bmb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                bmb.buffer = backing->ApiHandle();
                bmb.offset = static_cast<vk::DeviceSize>(bufferOffset);
                bmb.size = static_cast<vk::DeviceSize>(size);
                vk::DependencyInfo dep{};
                dep.bufferMemoryBarrierCount = 1;
                dep.pBufferMemoryBarriers = &bmb;
                cmd->ApiHandle().pipelineBarrier2(dep);

                pe::BufferTrackInfo &trackInfo = backing->GetTrackInfo();
                trackInfo.stageMask = vk::PipelineStageFlagBits2::eAllCommands;
                trackInfo.accessMask =
                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
            }

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
        auto fail = [&](const char *msg)
        {
            if (queue->device)
            {
                std::string full = std::string("writeTexture: ") + msg;
                queue->device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(full));
            }
        };
        if (!destination || !destination->texture || !dataLayout || !writeSize)
        {
            fail("null descriptor");
            return;
        }
        if (!data && dataSize > 0)
        {
            fail("data is null but dataSize > 0");
            return;
        }
        if (destination->texture->destroyed)
        {
            fail("destination texture is destroyed");
            return;
        }
        if (destination->texture->invalid || !destination->texture->image)
        {
            fail("destination texture is invalid");
            return;
        }
        if (destination->texture->device != queue->device)
        {
            fail("destination texture belongs to a different device");
            return;
        }
        if (destination->mipLevel >= destination->texture->mipLevelCount)
        {
            fail("mipLevel exceeds texture.mipLevelCount");
            return;
        }
        if (destination->texture->sampleCount > 1)
        {
            fail("destination texture must be single-sampled");
            return;
        }
        if (!(destination->texture->usage & WGPUTextureUsage_CopyDst))
        {
            fail("destination texture usage must include COPY_DST");
            return;
        }
        if (!IsDSWriteTextureAspectSupported(destination->texture->format, destination->aspect))
        {
            fail("aspect is not valid for writeTexture on this depth/stencil format");
            return;
        }
        {
            WGPUTextureImpl *tex = destination->texture;
            uint32_t mipW = std::max(1u, tex->size.width >> destination->mipLevel);
            uint32_t mipH = std::max(1u, tex->size.height >> destination->mipLevel);
            uint32_t mipD = (tex->dimension == WGPUTextureDimension_3D)
                                ? std::max(1u, tex->size.depthOrArrayLayers >> destination->mipLevel)
                                : tex->size.depthOrArrayLayers;
            uint32_t bw, bh;
            pwgpu::GetTexelBlockSize(tex->format, bw, bh);
            // W3C §11.2.6 "validating texture copy range": the bounds check
            // (origin + copySize ≤ subresourceSize) uses the *physical*
            // miplevel-specific texture extent — logical extent rounded up to
            // the texel block dimensions. For block-compressed formats whose
            // logical mip is smaller than one block (e.g. BC1 mip with logical
            // 1x1), the physical extent is one full block (4x4) and a
            // block-aligned copy of that block is in-bounds. The depth axis is
            // not block-rounded.
            const uint32_t physW = ((mipW + bw - 1) / bw) * bw;
            const uint32_t physH = ((mipH + bh - 1) / bh) * bh;
            uint64_t ox = destination->origin.x;
            uint64_t oy = destination->origin.y;
            uint64_t oz = destination->origin.z;
            if (ox + writeSize->width > physW ||
                oy + writeSize->height > physH ||
                oz + writeSize->depthOrArrayLayers > mipD)
            {
                fail("origin+writeSize exceeds mip extent");
                return;
            }
            if (pwgpu::HasDepthAspect(tex->format) || pwgpu::HasStencilAspect(tex->format))
            {
                if (ox != 0 || oy != 0 ||
                    writeSize->width != mipW || writeSize->height != mipH)
                {
                    fail("depth/stencil write must cover the entire mip");
                    return;
                }
            }
            if (bw > 1 || bh > 1)
            {
                if (ox % bw != 0 || oy % bh != 0)
                {
                    fail("origin is not block-aligned");
                    return;
                }
                if (ox + writeSize->width != physW && writeSize->width % bw != 0)
                {
                    fail("writeSize.width is not block-aligned");
                    return;
                }
                if (oy + writeSize->height != physH && writeSize->height % bh != 0)
                {
                    fail("writeSize.height is not block-aligned");
                    return;
                }
            }
        }

        pe::Image *image = destination->texture->image;
        WGPUTextureFormat fmt = destination->texture->format;
        bool is3D = (destination->texture->dimension == WGPUTextureDimension_3D);

        uint32_t blockW, blockH;
        pwgpu::GetTexelBlockSize(fmt, blockW, blockH);
        uint32_t footprint = pwgpu::TexelBlockCopyFootprint(fmt, destination->aspect);
        if (footprint == 0)
        {
            fail("format/aspect combination has no texel copy footprint");
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
                fail("bytesPerRow is required but not provided");
                return;
            }
            if (bprProvided && dataLayout->bytesPerRow < minBytesPerRow)
            {
                fail("bytesPerRow is below minimum for writeSize");
                return;
            }
            if (depth > 1 && !rpiProvided)
            {
                fail("rowsPerImage is required for multi-layer writes");
                return;
            }
            if (rpiProvided && dataLayout->rowsPerImage < heightInBlocks)
            {
                fail("rowsPerImage is below minimum for writeSize");
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
                fail("dataSize is smaller than required copy bytes");
                return;
            }
        }

        if (writeSize->width == 0 || writeSize->height == 0 || writeSize->depthOrArrayLayers == 0)
            return;

        vk::ImageAspectFlags vkAspect = pwgpu::ToVkAspect(destination->aspect, fmt);

        // §23.x lazy initialization: full-coverage write replaces the whole subresource;
        // partial write to an uninitialized subresource needs a zero-clear first so
        // unwritten texels read as zero.
        const uint32_t dstMipW = std::max(1u, destination->texture->size.width >> destination->mipLevel);
        const uint32_t dstMipH = std::max(1u, destination->texture->size.height >> destination->mipLevel);
        const uint32_t dstMipD = is3D ? std::max(1u, destination->texture->size.depthOrArrayLayers >> destination->mipLevel)
                                      : destination->texture->size.depthOrArrayLayers;
        const bool dstFullCoverage =
            destination->origin.x == 0 && destination->origin.y == 0 && destination->origin.z == 0 &&
            writeSize->width == dstMipW && writeSize->height == dstMipH &&
            writeSize->depthOrArrayLayers == dstMipD;
        const uint32_t dstBaseLayer = is3D ? 0u : destination->origin.z;
        const uint32_t dstLayerCount = is3D ? 1u : writeSize->depthOrArrayLayers;
        auto dstAspects = pwgpu::AspectsForView(fmt, destination->aspect);

        pe::CommandBuffer *cmd = queue->peQueue->AcquireCommandBuffer();
        cmd->Begin();

        bool didLazyInit = false;
        if (!dstFullCoverage &&
            pwgpu::RangeHasAnyUninitialized(destination->texture, destination->mipLevel, 1,
                                            dstBaseLayer, dstLayerCount, dstAspects))
        {
            pwgpu::LazyInitViewRangeOnEncoder(cmd, destination->texture, destination->mipLevel, 1,
                                              dstBaseLayer, dstLayerCount, dstAspects);
            didLazyInit = true;
        }

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

        // Order the lazy-clear's transferWrite before the upcoming copy's transferWrite.
        if (didLazyInit)
        {
            vk::MemoryBarrier2 mb{};
            mb.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            mb.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            mb.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
            mb.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
            cmd->MemoryBarrier(mb);
        }

        // §19.3: dataLayout.offset is into the user's CPU buffer, not the GPU staging buffer.
        // The user's bytesPerRow / rowsPerImage may not be representable as Vulkan's
        // (bufferRowLength, bufferImageHeight): Vulkan addresses the source as a 3D grid
        // of texels (or compressed blocks) where the byte stride between rows is exactly
        // bufferRowLength * texelBlockSize / blockW. WebGPU writeTexture imposes no such
        // constraint on bytesPerRow (e.g. CTS issues bytesPerRow = 31 for an rgba8unorm
        // copy of width 4 → minBytesPerRow 16, padding 15 → 31, which has no integer
        // bufferRowLength because 31 is not a multiple of footprint=4). Repack the source
        // CPU data into a tightly-packed staging layout (widthInBlocks * footprint per row,
        // heightInBlocks rows per layer) so we can leave bufferRowLength / bufferImageHeight
        // unset and Vulkan derives them from imageExtent. This also avoids leaking user
        // dataLayout.offset alignment to Vulkan (depth/stencil formats require GPU
        // bufferOffset to be a multiple of 4 per VUID-VkCopyBufferToImageInfo2-dstImage-07978).
        const uint32_t widthInBlocks = (writeSize->width + blockW - 1) / blockW;
        const uint32_t heightInBlocks = (writeSize->height + blockH - 1) / blockH;
        const uint32_t copyDepth = writeSize->depthOrArrayLayers;
        const uint64_t tightBytesPerRow = static_cast<uint64_t>(widthInBlocks) * footprint;
        const uint64_t tightRowsPerImage = heightInBlocks;
        const uint64_t tightBytesPerImage = tightBytesPerRow * tightRowsPerImage;
        const uint64_t stagingBytes = tightBytesPerImage * copyDepth;
        pe::StagingAllocation alloc = pe::RHII.GetStagingManager()->Allocate(stagingBytes);
        if (stagingBytes > 0 && data)
        {
            const bool bprProvided = (dataLayout->bytesPerRow != WGPU_COPY_STRIDE_UNDEFINED);
            const bool rpiProvided = (dataLayout->rowsPerImage != WGPU_COPY_STRIDE_UNDEFINED);
            const uint64_t srcBytesPerRow = bprProvided ? dataLayout->bytesPerRow : tightBytesPerRow;
            const uint64_t srcRowsPerImage = rpiProvided ? dataLayout->rowsPerImage : heightInBlocks;
            const uint64_t srcBytesPerImage = srcBytesPerRow * srcRowsPerImage;
            const uint8_t *srcBase =
                static_cast<const uint8_t *>(data) + static_cast<size_t>(dataLayout->offset);
            uint8_t *dstBase = static_cast<uint8_t *>(alloc.data);
            for (uint32_t z = 0; z < copyDepth; ++z)
            {
                for (uint32_t y = 0; y < heightInBlocks; ++y)
                {
                    const uint8_t *srcRow = srcBase + z * srcBytesPerImage + y * srcBytesPerRow;
                    uint8_t *dstRow = dstBase + z * tightBytesPerImage + y * tightBytesPerRow;
                    std::memcpy(dstRow, srcRow, static_cast<size_t>(tightBytesPerRow));
                }
            }
        }
        if (stagingBytes > 0)
            alloc.buffer->Flush(stagingBytes, 0);

        vk::BufferImageCopy2 region{};
        region.bufferOffset = 0;
        // bufferRowLength = 0 / bufferImageHeight = 0 means "tightly packed, derived from
        // imageExtent" — we pre-packed the staging buffer above to match exactly that.
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
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

        // §23.x: dst (mip, [baseLayer..baseLayer+layerCount)) is fully initialized after
        // the write — full coverage overwrites everything; partial writes were preceded
        // by a lazy-clear.
        pwgpu::MarkRangeInitialized(destination->texture, destination->mipLevel, 1,
                                    dstBaseLayer, dstLayerCount, dstAspects);

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
