#include "Device.h"
#include "Buffer.h"
#include "Texture.h"
#include "CommandEncoder.h"
#include "FormatMap.h"
#include "Utils.h"
#include "API/Queue.h"
#include "API/Image.h"
#include "API/Buffer.h"
#include "API/RHI.h"
#include "API/StagingManager.h"

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

        for (size_t i = 0; i < commandCount; ++i)
        {
            WGPUCommandBuffer cb = commands[i];
            if (!cb || !cb->cmd || cb->submitted)
                continue;

            cb->submitted = true;
            pe::CommandBuffer *cmd = cb->cmd;
            queue->peQueue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            cmd->Return();
            cb->cmd = nullptr;
        }
    }

    // ---- §19.2 writeBuffer (immediate) ----

    void wgpuQueueWriteBuffer(WGPUQueue queue, WGPUBuffer buffer,
                              uint64_t bufferOffset, void const *data, size_t size)
    {
        (void)queue;
        if (!buffer || !buffer->peBuffer || !data || buffer->destroyed)
            return;
        if (!(buffer->usage & WGPUBufferUsage_CopyDst))
            return;
        if (bufferOffset % 4 != 0)
            return;
        if (size % 4 != 0)
            return;
        if (bufferOffset + size > buffer->size)
            return;

        if (buffer->hostVisible)
        {
            pe::BufferRange range{const_cast<void *>(data), size, static_cast<size_t>(bufferOffset)};
            buffer->peBuffer->Copy(1, &range, false);
        }
        else if (queue && queue->peQueue)
        {
            pe::CommandBuffer *cmd = queue->peQueue->AcquireCommandBuffer();
            cmd->Begin();
            cmd->CopyBufferStaged(buffer->peBuffer, const_cast<void *>(data), size, static_cast<size_t>(bufferOffset));
            cmd->End();
            queue->peQueue->Submit(1, &cmd, nullptr, nullptr);
            cmd->Wait();
            cmd->Return();
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
        if (!destination || !destination->texture || !destination->texture->image || destination->texture->destroyed)
            return;
        if (!data || !dataSize || !dataLayout || !writeSize)
            return;
        if (!(destination->texture->usage & WGPUTextureUsage_CopyDst))
            return;

        pe::Image *image = destination->texture->image;
        WGPUTextureFormat fmt = destination->texture->format;
        bool is3D = (destination->texture->dimension == WGPUTextureDimension_3D);

        uint32_t blockW, blockH;
        pwgpu::GetTexelBlockSize(fmt, blockW, blockH);
        uint32_t footprint = pwgpu::TexelBlockCopyFootprint(fmt, destination->aspect);
        if (footprint == 0)
            return;

        {
            uint32_t widthInBlocks = (writeSize->width + blockW - 1) / blockW;
            uint32_t heightInBlocks = (writeSize->height + blockH - 1) / blockH;
            uint32_t minBytesPerRow = widthInBlocks * footprint;
            if (dataLayout->bytesPerRow < minBytesPerRow)
                return;
            uint32_t depth = writeSize->depthOrArrayLayers;
            if (depth > 0)
            {
                uint64_t bytesPerImage = static_cast<uint64_t>(dataLayout->bytesPerRow) * dataLayout->rowsPerImage;
                uint64_t lastRowBytes = static_cast<uint64_t>(dataLayout->bytesPerRow) * (heightInBlocks - 1) + minBytesPerRow;
                uint64_t required = dataLayout->offset + (depth > 1 ? bytesPerImage * (depth - 1) : 0) + lastRowBytes;
                if (required > dataSize)
                    return;
            }
        }

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
        region.bufferRowLength = (footprint > 0) ? (dataLayout->bytesPerRow / footprint) * blockW : 0;
        region.bufferImageHeight = dataLayout->rowsPerImage * blockH;
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
        cmd->Wait();
        cmd->Return();
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
