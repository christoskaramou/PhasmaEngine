#include "Texture.h"
#include "Device.h"
#include "Utils.h"
#include "FormatMap.h"
#include "API/Buffer.h"
#include "API/Queue.h"
#include "API/Vulkan/VulkanBufferImpl.h"
#include "API/Vulkan/VulkanImageImpl.h"
#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/RHI.h"
#include "API/Semaphore.h"
#include "API/Command.h"
#include "API/StagingManager.h"
#include <cstring>

namespace pwgpu
{
    // §23.x lazy initialization key packing.
    static uint64_t MakeSubresourceKey(uint32_t mip, uint32_t layer, uint8_t aspect)
    {
        return (static_cast<uint64_t>(aspect) << 56) |
               (static_cast<uint64_t>(mip & 0xFFFFFFu) << 32) |
               static_cast<uint64_t>(layer);
    }

    // Aspects relevant to a subresource for a given format. For depth-stencil formats we
    // track depth and stencil aspects independently because storeOp/loadOp + render-pass
    // depthReadOnly/stencilReadOnly can diverge between aspects.
    std::vector<uint8_t> AspectsForView(WGPUTextureFormat fmt, WGPUTextureAspect viewAspect)
    {
        std::vector<uint8_t> out;
        const bool hasD = HasDepthAspect(fmt);
        const bool hasS = HasStencilAspect(fmt);
        if (!hasD && !hasS)
        {
            out.push_back(kAspectColor);
            return out;
        }
        switch (viewAspect)
        {
        case WGPUTextureAspect_DepthOnly:
            if (hasD)
                out.push_back(kAspectDepth);
            break;
        case WGPUTextureAspect_StencilOnly:
            if (hasS)
                out.push_back(kAspectStencil);
            break;
        case WGPUTextureAspect_All:
        default:
            if (hasD)
                out.push_back(kAspectDepth);
            if (hasS)
                out.push_back(kAspectStencil);
            break;
        }
        return out;
    }

    bool IsSubresourceInitialized(WGPUTextureImpl *tex, uint32_t mip, uint32_t layer, uint8_t aspect)
    {
        if (!tex)
            return true;
        std::lock_guard<std::mutex> lk(tex->stateMutex);
        return tex->uninitializedSubresources.find(MakeSubresourceKey(mip, layer, aspect)) ==
               tex->uninitializedSubresources.end();
    }

    void MarkSubresourceInitialized(WGPUTextureImpl *tex, uint32_t mip, uint32_t layer, uint8_t aspect)
    {
        if (!tex)
            return;
        std::lock_guard<std::mutex> lk(tex->stateMutex);
        tex->uninitializedSubresources.erase(MakeSubresourceKey(mip, layer, aspect));
    }

    void MarkSubresourceUninitialized(WGPUTextureImpl *tex, uint32_t mip, uint32_t layer, uint8_t aspect)
    {
        if (!tex)
            return;
        std::lock_guard<std::mutex> lk(tex->stateMutex);
        tex->uninitializedSubresources.insert(MakeSubresourceKey(mip, layer, aspect));
    }

    // Range helpers — iterate every (mip,layer,aspect) tuple covered by a view.
    void MarkRangeInitialized(WGPUTextureImpl *tex, uint32_t baseMip, uint32_t mipCount,
                              uint32_t baseLayer, uint32_t layerCount,
                              const std::vector<uint8_t> &aspects)
    {
        if (!tex)
            return;
        std::lock_guard<std::mutex> lk(tex->stateMutex);
        for (uint32_t m = baseMip; m < baseMip + mipCount; ++m)
            for (uint32_t l = baseLayer; l < baseLayer + layerCount; ++l)
                for (uint8_t a : aspects)
                    tex->uninitializedSubresources.erase(MakeSubresourceKey(m, l, a));
    }

    void MarkRangeUninitialized(WGPUTextureImpl *tex, uint32_t baseMip, uint32_t mipCount,
                                uint32_t baseLayer, uint32_t layerCount,
                                const std::vector<uint8_t> &aspects)
    {
        if (!tex)
            return;
        std::lock_guard<std::mutex> lk(tex->stateMutex);
        for (uint32_t m = baseMip; m < baseMip + mipCount; ++m)
            for (uint32_t l = baseLayer; l < baseLayer + layerCount; ++l)
                for (uint8_t a : aspects)
                    tex->uninitializedSubresources.insert(MakeSubresourceKey(m, l, a));
    }

    bool RangeHasAnyUninitialized(WGPUTextureImpl *tex,
                                  uint32_t baseMip, uint32_t mipCount,
                                  uint32_t baseLayer, uint32_t layerCount,
                                  const std::vector<uint8_t> &aspects)
    {
        if (!tex)
            return false;
        std::lock_guard<std::mutex> lk(tex->stateMutex);
        for (uint8_t a : aspects)
            for (uint32_t m = baseMip; m < baseMip + mipCount; ++m)
                for (uint32_t l = baseLayer; l < baseLayer + layerCount; ++l)
                    if (tex->uninitializedSubresources.count(MakeSubresourceKey(m, l, a)))
                        return true;
        return false;
    }

    static uint32_t MipExtent(uint32_t base, uint32_t mip)
    {
        return std::max(1u, base >> mip);
    }

    static vk::DeviceSize CompressedSubresourceByteSize(WGPUTextureImpl *tex, uint32_t mip)
    {
        uint32_t blockW = 1;
        uint32_t blockH = 1;
        GetTexelBlockSize(tex->format, blockW, blockH);
        const uint32_t footprint = TexelBlockCopyFootprint(tex->format);
        const uint32_t mipW = MipExtent(tex->size.width, mip);
        const uint32_t mipH = MipExtent(tex->size.height, mip);
        const uint32_t mipD = (tex->dimension == WGPUTextureDimension_3D)
                                  ? MipExtent(tex->size.depthOrArrayLayers, mip)
                                  : 1u;
        const uint64_t blocksX = (static_cast<uint64_t>(mipW) + blockW - 1u) / blockW;
        const uint64_t blocksY = (static_cast<uint64_t>(mipH) + blockH - 1u) / blockH;
        return static_cast<vk::DeviceSize>(blocksX * blocksY * mipD * footprint);
    }

    static bool RecordCompressedZeroCopy(pe::CommandBuffer *cmd, WGPUTextureImpl *tex,
                                         const std::vector<std::pair<uint32_t, uint32_t>> &mipLayerPairs,
                                         vk::ImageAspectFlags aspectMask)
    {
        if (!pwgpu::IsCompressedFormat(tex->format) ||
            aspectMask != vk::ImageAspectFlagBits::eColor)
            return false;

        if (mipLayerPairs.empty())
            return true;

        // Vulkan forbids vkCmdClearColorImage on compressed images, so zeroing happens via
        // copyBufferToImage from a host-visible buffer of zeros. Sizing that buffer to the
        // sum of all touched subresources scales with the full mip chain. Allocate exactly
        // the largest single subresource instead and replay it via bufferOffset=0 for every
        // region — the GPU reads the same zero range N times. Worst-case host memory drops
        // from O(mip0 * 4/3) to O(mip0), and StagingManager::Allocate failure now affects
        // only the largest subresource's footprint.
        vk::DeviceSize maxBytes = 0;
        for (const auto &p : mipLayerPairs)
        {
            const vk::DeviceSize bytes = CompressedSubresourceByteSize(tex, p.first);
            if (bytes > maxBytes)
                maxBytes = bytes;
        }
        if (maxBytes == 0)
            return true;

        pe::StagingAllocation alloc =
            pe::RHII.GetStagingManager()->Allocate(static_cast<size_t>(maxBytes));
        std::memset(alloc.data, 0, static_cast<size_t>(maxBytes));
        alloc.buffer->Flush(static_cast<size_t>(maxBytes), 0);

        std::vector<vk::BufferImageCopy2> regions;
        regions.reserve(mipLayerPairs.size());
        for (const auto &p : mipLayerPairs)
        {
            const uint32_t mip = p.first;
            const uint32_t mipW = MipExtent(tex->size.width, mip);
            const uint32_t mipH = MipExtent(tex->size.height, mip);
            const uint32_t mipD = (tex->dimension == WGPUTextureDimension_3D)
                                      ? MipExtent(tex->size.depthOrArrayLayers, mip)
                                      : 1u;

            vk::BufferImageCopy2 region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer =
                (tex->dimension == WGPUTextureDimension_3D) ? 0u : p.second;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = vk::Offset3D{0, 0, 0};
            region.imageExtent = vk::Extent3D{mipW, mipH, mipD};
            regions.push_back(region);
        }

        vk::CopyBufferToImageInfo2 copyInfo{};
        copyInfo.srcBuffer = pe::GetVulkanBuffer(alloc.buffer);
        copyInfo.dstImage = pe::GetVulkanImage(tex->image);
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = static_cast<uint32_t>(regions.size());
        copyInfo.pRegions = regions.data();
        cmd->ApiHandle().copyBufferToImage2(copyInfo);

        cmd->AddAfterWaitCallback([alloc = std::move(alloc)]() mutable
                                  { pe::RHII.GetStagingManager()->SetUnused(alloc); });
        return true;
    }

    // Records zeroing covering only the explicitly enumerated (mip, layer) subresources
    // for a single aspect. Clear commands handle ordinary formats; compressed color
    // images use zero-buffer copies because Vulkan forbids clearing compressed images.
    void RecordZeroInitTextureSubresources(pe::CommandBuffer *cmd, WGPUTextureImpl *tex,
                                           const std::vector<std::pair<uint32_t, uint32_t>> &mipLayerPairs,
                                           vk::ImageAspectFlags aspectMask)
    {
        if (!cmd || !tex || !tex->image || mipLayerPairs.empty())
            return;

        // Compute bounding box for the layout transition. A layout transition on
        // already-initialized neighbors is safe (data is preserved); only the actual zero
        // operations below are data-destructive, and they list exact subresources.
        uint32_t minMip = UINT32_MAX, maxMip = 0, minLayer = UINT32_MAX, maxLayer = 0;
        for (const auto &p : mipLayerPairs)
        {
            minMip = std::min(minMip, p.first);
            maxMip = std::max(maxMip, p.first);
            minLayer = std::min(minLayer, p.second);
            maxLayer = std::max(maxLayer, p.second);
        }

        pe::ImageBarrierInfo toTransfer{};
        toTransfer.image = tex->image;
        toTransfer.layout = PE_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.stageFlags = PE_STAGE_TRANSFER;
        toTransfer.accessMask = PE_ACCESS_TRANSFER_WRITE;
        toTransfer.baseMipLevel = minMip;
        toTransfer.mipLevels = maxMip - minMip + 1;
        toTransfer.baseArrayLayer = minLayer;
        toTransfer.arrayLayers = maxLayer - minLayer + 1;
        cmd->ImageBarrier(toTransfer);

        if (RecordCompressedZeroCopy(cmd, tex, mipLayerPairs, aspectMask))
            return;

        std::vector<vk::ImageSubresourceRange> ranges;
        ranges.reserve(mipLayerPairs.size());
        for (const auto &p : mipLayerPairs)
        {
            vk::ImageSubresourceRange r{};
            r.aspectMask = aspectMask;
            r.baseMipLevel = p.first;
            r.levelCount = 1;
            r.baseArrayLayer = p.second;
            r.layerCount = 1;
            ranges.push_back(r);
        }

        if (IsDepthStencilFormat(tex->format))
        {
            vk::ClearDepthStencilValue dsValue{0.0f, 0u};
            cmd->ApiHandle().clearDepthStencilImage(pe::GetVulkanImage(tex->image),
                                                    vk::ImageLayout::eTransferDstOptimal,
                                                    &dsValue,
                                                    static_cast<uint32_t>(ranges.size()),
                                                    ranges.data());
        }
        else
        {
            vk::ClearColorValue colorValue{};
            colorValue.float32[0] = 0.0f;
            colorValue.float32[1] = 0.0f;
            colorValue.float32[2] = 0.0f;
            colorValue.float32[3] = 0.0f;
            cmd->ApiHandle().clearColorImage(pe::GetVulkanImage(tex->image),
                                             vk::ImageLayout::eTransferDstOptimal,
                                             &colorValue,
                                             static_cast<uint32_t>(ranges.size()),
                                             ranges.data());
        }
    }

    // Lazy-clear any uninitialized subresource within a view's range, recording into the
    // encoder's command buffer. Marks them initialized afterwards. Returns true iff any
    // clear was emitted (caller may want to know whether to insert a follow-up barrier).
    //
    // Only the individual (mip, layer) subresources that are currently tracked as
    // uninitialized are cleared; untouched subresources in the same bounding range
    // keep their data. Previously this function issued a fused clear over the full
    // range, which destroyed canary data written by the user into subresources that
    // happened to share a view with a storeOp=Discard'd sibling. That over-clear
    // accounted for the bulk of the Sample/CopyToBuffer/CopyToTexture failures in
    // `webgpu:api,operation,resource_init,*` when `mipLevelCount>1` and
    // `uninitializeMethod="StoreOpClear"` (~148 subcase fails).
    bool LazyInitViewRangeOnEncoder(pe::CommandBuffer *cmd, WGPUTextureImpl *tex,
                                    uint32_t baseMip, uint32_t mipCount,
                                    uint32_t baseLayer, uint32_t layerCount,
                                    const std::vector<uint8_t> &aspects)
    {
        if (!cmd || !tex || !tex->image)
            return false;

        bool emittedAny = false;
        std::vector<std::pair<uint32_t, uint32_t>> toClear;
        {
            std::lock_guard<std::mutex> lk(tex->stateMutex);
            for (uint8_t a : aspects)
            {
                toClear.clear();
                for (uint32_t m = baseMip; m < baseMip + mipCount; ++m)
                    for (uint32_t l = baseLayer; l < baseLayer + layerCount; ++l)
                        if (tex->uninitializedSubresources.count(MakeSubresourceKey(m, l, a)))
                            toClear.emplace_back(m, l);
                if (toClear.empty())
                    continue;

                vk::ImageAspectFlags aspectMask{};
                if (a == kAspectDepth)
                    aspectMask = vk::ImageAspectFlagBits::eDepth;
                else if (a == kAspectStencil)
                    aspectMask = vk::ImageAspectFlagBits::eStencil;
                else
                    aspectMask = vk::ImageAspectFlagBits::eColor;

                RecordZeroInitTextureSubresources(cmd, tex, toClear, aspectMask);
                emittedAny = true;

                for (const auto &p : toClear)
                    tex->uninitializedSubresources.erase(MakeSubresourceKey(p.first, p.second, a));
            }
        }
        return emittedAny;
    }

    // Eager clear via a fresh command-buffer submission (used from createView when there
    // is no encoder context). Mirrors the buffer/texture eager-init pattern in Device.cpp.
    // Clears only the specific (mip, layer) subresources currently flagged uninitialized,
    // leaving all other subresources in the view's bounding range untouched so canary
    // data written by the user to initialized siblings survives.
    void LazyInitViewRangeViaQueue(WGPUTextureImpl *tex,
                                   uint32_t baseMip, uint32_t mipCount,
                                   uint32_t baseLayer, uint32_t layerCount,
                                   const std::vector<uint8_t> &aspects)
    {
        if (!tex || !tex->image || !tex->device || !tex->device->queue ||
            !tex->device->queue->peQueue)
            return;

        // Under a single lock: snapshot exact (aspect -> list of (mip,layer)) pairs that
        // need clearing, then mark them initialized so concurrent lookups see the state
        // change immediately. The GPU work itself doesn't need the lock held.
        std::vector<std::pair<uint8_t, std::vector<std::pair<uint32_t, uint32_t>>>> work;
        {
            std::lock_guard<std::mutex> lk(tex->stateMutex);
            for (uint8_t a : aspects)
            {
                std::vector<std::pair<uint32_t, uint32_t>> pairs;
                for (uint32_t m = baseMip; m < baseMip + mipCount; ++m)
                    for (uint32_t l = baseLayer; l < baseLayer + layerCount; ++l)
                        if (tex->uninitializedSubresources.count(MakeSubresourceKey(m, l, a)))
                            pairs.emplace_back(m, l);
                if (pairs.empty())
                    continue;
                for (const auto &p : pairs)
                    tex->uninitializedSubresources.erase(MakeSubresourceKey(p.first, p.second, a));
                work.emplace_back(a, std::move(pairs));
            }
            if (work.empty())
                return;
        }

        WGPUQueueImpl *q = tex->device->queue;
        pe::CommandBuffer *cmd = q->peQueue->AcquireCommandBuffer();
        cmd->Begin();
        for (const auto &entry : work)
        {
            vk::ImageAspectFlags aspectMask{};
            if (entry.first == kAspectDepth)
                aspectMask = vk::ImageAspectFlagBits::eDepth;
            else if (entry.first == kAspectStencil)
                aspectMask = vk::ImageAspectFlagBits::eStencil;
            else
                aspectMask = vk::ImageAspectFlagBits::eColor;
            RecordZeroInitTextureSubresources(cmd, tex, entry.second, aspectMask);
        }
        cmd->End();
        q->peQueue->Submit(1, &cmd, nullptr, nullptr);
        const uint64_t serial = q->peQueue->GetSubmissionCount();
        {
            std::lock_guard<std::mutex> lock(q->pendingMutex);
            q->pendingSubmits.push_back({cmd, serial});
        }
        q->lastSubmissionSerial.store(serial, std::memory_order_release);
        tex->lastUsageSerial.store(serial, std::memory_order_release);
    }
} // namespace pwgpu

void TeardownTextureGpuResources(WGPUTextureImpl *texture)
{
    {
        std::lock_guard<std::mutex> lock(texture->childViewsMutex);
        for (auto *cv : texture->childViews)
        {
            if (cv && cv->view)
            {
                pe::ImageView::Destroy(cv->view);
                cv->view = nullptr;
            }
        }
        texture->childViews.clear();
    }
    if (texture->image && !texture->isSwapchain)
    {
        pe::Image::Destroy(texture->image);
        texture->image = nullptr;
    }
}

extern "C"
{

    void wgpuTextureAddRef(WGPUTexture texture)
    {
        if (texture)
            texture->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuTextureRelease(WGPUTexture texture)
    {
        if (!texture)
            return;
        if (texture->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            if (texture->image && !texture->destroyed && !texture->isSwapchain)
            {
                const uint64_t lastUsage = texture->lastUsageSerial.load(std::memory_order_acquire);
                if (lastUsage != 0 && texture->device && texture->device->queue)
                {
                    pe::Semaphore *sem = texture->device->queue->GetSemaphore();
                    if (sem && sem->GetValue() < lastUsage)
                        sem->WaitTimeout(lastUsage, UINT64_MAX);
                }
                pe::Image::Destroy(texture->image);
            }
            if (texture->device)
                wgpuDeviceRelease(texture->device);
            delete texture;
        }
    }

    void wgpuTextureDestroy(WGPUTexture texture)
    {
        if (!texture || texture->destroyed)
            return;
        texture->destroyed = true;

        const uint64_t lastUsage = texture->lastUsageSerial.load(std::memory_order_acquire);
        if (lastUsage == 0 || !texture->device || !texture->device->queue)
        {
            TeardownTextureGpuResources(texture);
            return;
        }

        WGPUQueueImpl *queue = texture->device->queue;
        pe::Semaphore *sem = queue->GetSemaphore();
        const uint64_t completed = sem ? sem->GetValue() : 0;

        if (lastUsage <= completed)
        {
            TeardownTextureGpuResources(texture);
        }
        else
        {
            wgpuTextureAddRef(texture);
            std::lock_guard<std::mutex> lock(texture->device->pendingTextureDeletionsMutex);
            texture->device->pendingTextureDeletions.push_back({texture, lastUsage});
        }
    }

    WGPUTextureView wgpuTextureCreateView(WGPUTexture texture, WGPUTextureViewDescriptor const *descriptor)
    {
        if (!texture)
            return nullptr;

        auto reportValidation = [&](const char *what) -> WGPUTextureView
        {
            if (what && texture->device)
            {
                std::string msg = std::string("wgpuTextureCreateView: ") + what;
                texture->device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(msg));
            }
            auto *bad = new WGPUTextureViewImpl();
            wgpuTextureAddRef(texture);
            bad->texture = texture;
            if (descriptor && descriptor->label.data)
                bad->label = pwgpu::ToString(descriptor->label);
            return bad;
        };

        if (descriptor &&
            !pwgpu::ValidateChainedStruct(
                texture->device, descriptor->nextInChain,
                {WGPUSType_TextureComponentSwizzleDescriptor},
                "wgpuTextureCreateView", "WGPUTextureViewDescriptor"))
            return reportValidation(nullptr);

        if (texture->invalid)
            return reportValidation("texture is invalid");

        // Destroyed texture use as an attachment is reported at queue.submit() (§3.3).
        // Return a quiet stub view so encoders can retain it without touching a null
        // native image/view.
        if (texture->destroyed)
            return reportValidation(nullptr);

        WGPUTextureViewDescriptor resolved{};
        resolved.format = WGPUTextureFormat_Undefined;
        resolved.dimension = WGPUTextureViewDimension_Undefined;
        resolved.baseMipLevel = 0;
        resolved.mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
        resolved.baseArrayLayer = 0;
        resolved.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
        resolved.aspect = WGPUTextureAspect_Undefined;
        resolved.usage = WGPUTextureUsage_None;
        if (descriptor)
            resolved = *descriptor;

        if (resolved.aspect == WGPUTextureAspect_Undefined)
            resolved.aspect = WGPUTextureAspect_All;

        if (resolved.format == WGPUTextureFormat_Undefined)
        {
            if (resolved.aspect != WGPUTextureAspect_All)
            {
                WGPUTextureFormat aspectFmt = pwgpu::ResolveAspectFormat(texture->format, resolved.aspect);
                resolved.format = (aspectFmt != WGPUTextureFormat_Undefined) ? aspectFmt : texture->format;
            }
            else
            {
                resolved.format = texture->format;
            }
        }

        // Early range checks before resolving defaults to prevent unsigned wraparound.
        if (resolved.baseMipLevel >= texture->mipLevelCount)
            return reportValidation("baseMipLevel is out of range");

        const uint32_t textureArrayLayers = (texture->dimension == WGPUTextureDimension_2D)
                                                ? texture->size.depthOrArrayLayers
                                                : 1u;

        if (resolved.baseArrayLayer >= textureArrayLayers)
            return reportValidation("baseArrayLayer is out of range");

        if (resolved.mipLevelCount == WGPU_MIP_LEVEL_COUNT_UNDEFINED)
            resolved.mipLevelCount = texture->mipLevelCount - resolved.baseMipLevel;

        if (resolved.dimension == WGPUTextureViewDimension_Undefined)
        {
            switch (texture->dimension)
            {
            case WGPUTextureDimension_1D:
                resolved.dimension = WGPUTextureViewDimension_1D;
                break;
            case WGPUTextureDimension_2D:
                resolved.dimension = (texture->size.depthOrArrayLayers == 1)
                                         ? WGPUTextureViewDimension_2D
                                         : WGPUTextureViewDimension_2DArray;
                break;
            case WGPUTextureDimension_3D:
                resolved.dimension = WGPUTextureViewDimension_3D;
                break;
            default:
                resolved.dimension = WGPUTextureViewDimension_2D;
                break;
            }
        }

        if (resolved.arrayLayerCount == WGPU_ARRAY_LAYER_COUNT_UNDEFINED)
        {
            switch (resolved.dimension)
            {
            case WGPUTextureViewDimension_1D:
            case WGPUTextureViewDimension_2D:
            case WGPUTextureViewDimension_3D:
                resolved.arrayLayerCount = 1;
                break;
            case WGPUTextureViewDimension_Cube:
                resolved.arrayLayerCount = 6;
                break;
            case WGPUTextureViewDimension_2DArray:
            case WGPUTextureViewDimension_CubeArray:
                resolved.arrayLayerCount = textureArrayLayers - resolved.baseArrayLayer;
                break;
            default:
                resolved.arrayLayerCount = 1;
                break;
            }
        }

        if (resolved.usage == WGPUTextureUsage_None)
            resolved.usage = texture->usage;

        if (!pwgpu::AspectPresentInFormat(resolved.aspect, texture->format))
            return reportValidation("aspect not present in texture format");

        if (resolved.aspect == WGPUTextureAspect_All)
        {
            bool fmtOk = (resolved.format == texture->format);
            if (!fmtOk)
            {
                for (auto vf : texture->viewFormats)
                {
                    if (vf == resolved.format)
                    {
                        fmtOk = true;
                        break;
                    }
                }
            }
            if (!fmtOk)
                return reportValidation("view format must equal texture format or be in viewFormats");
        }
        else
        {
            WGPUTextureFormat expected = pwgpu::ResolveAspectFormat(texture->format, resolved.aspect);
            if (expected != WGPUTextureFormat_Undefined && resolved.format != expected)
                return reportValidation("view format must equal the resolved aspect format");
        }

        WGPUTextureUsage viewUsage = static_cast<WGPUTextureUsage>(resolved.usage);
        if ((viewUsage & ~texture->usage) != 0)
            return reportValidation("view usage must be a subset of texture usage");

        const bool tier1 =
            texture->device &&
            wgpuDeviceHasFeature(texture->device, WGPUFeatureName_TextureFormatsTier1) == WGPU_TRUE;
        if (viewUsage & WGPUTextureUsage_RenderAttachment)
        {
            if (!pwgpu::IsRenderableFormat(resolved.format) &&
                !(tier1 && pwgpu::IsRenderableFormatTier1(resolved.format)))
                return reportValidation("RENDER_ATTACHMENT requires a renderable view format");
            if (resolved.format == WGPUTextureFormat_RG11B10Ufloat &&
                wgpuDeviceHasFeature(texture->device,
                                     WGPUFeatureName_RG11B10UfloatRenderable) != WGPU_TRUE)
                return reportValidation(
                    "RG11B10Ufloat + RENDER_ATTACHMENT requires RG11B10UfloatRenderable feature");
        }
        if (viewUsage & WGPUTextureUsage_StorageBinding)
        {
            if (!pwgpu::SupportsStorageBinding(resolved.format) &&
                !(tier1 && pwgpu::SupportsStorageBindingTier1(resolved.format)))
                return reportValidation("STORAGE_BINDING requires a storage-capable view format");
            if (resolved.format == WGPUTextureFormat_BGRA8Unorm &&
                wgpuDeviceHasFeature(texture->device,
                                     WGPUFeatureName_BGRA8UnormStorage) != WGPU_TRUE)
                return reportValidation(
                    "BGRA8Unorm + STORAGE_BINDING requires BGRA8UnormStorage feature");
        }

        if (resolved.mipLevelCount == 0)
            return reportValidation("mipLevelCount must be > 0");
        if (uint64_t(resolved.baseMipLevel) + resolved.mipLevelCount > texture->mipLevelCount)
            return reportValidation("baseMipLevel + mipLevelCount exceeds texture mipLevelCount");

        if (resolved.arrayLayerCount == 0)
            return reportValidation("arrayLayerCount must be > 0");
        if (uint64_t(resolved.baseArrayLayer) + resolved.arrayLayerCount > textureArrayLayers)
            return reportValidation("baseArrayLayer + arrayLayerCount exceeds texture array layers");

        if (texture->sampleCount > 1 && resolved.dimension != WGPUTextureViewDimension_2D)
            return reportValidation("multisampled texture view dimension must be 2d");

        const WGPUTextureComponentSwizzleDescriptor *swizzleExt =
            pwgpu::FindChained<WGPUTextureComponentSwizzleDescriptor>(
                resolved.nextInChain, WGPUSType_TextureComponentSwizzleDescriptor);
        WGPUComponentSwizzle sR = WGPUComponentSwizzle_R;
        WGPUComponentSwizzle sG = WGPUComponentSwizzle_G;
        WGPUComponentSwizzle sB = WGPUComponentSwizzle_B;
        WGPUComponentSwizzle sA = WGPUComponentSwizzle_A;
        auto isIdentitySwizzle = [&]()
        {
            return sR == WGPUComponentSwizzle_R && sG == WGPUComponentSwizzle_G &&
                   sB == WGPUComponentSwizzle_B && sA == WGPUComponentSwizzle_A;
        };
        if (swizzleExt)
        {
            auto resolveSwizzle = [](WGPUComponentSwizzle v, WGPUComponentSwizzle identity)
            {
                return (v == WGPUComponentSwizzle_Undefined) ? identity : v;
            };
            sR = resolveSwizzle(swizzleExt->swizzle.r, WGPUComponentSwizzle_R);
            sG = resolveSwizzle(swizzleExt->swizzle.g, WGPUComponentSwizzle_G);
            sB = resolveSwizzle(swizzleExt->swizzle.b, WGPUComponentSwizzle_B);
            sA = resolveSwizzle(swizzleExt->swizzle.a, WGPUComponentSwizzle_A);

            const bool isIdentity = isIdentitySwizzle();
            const bool featureEnabled =
                texture->device &&
                wgpuDeviceHasFeature(texture->device,
                                     WGPUFeatureName_TextureComponentSwizzle) == WGPU_TRUE;
            if (!isIdentity && !featureEnabled)
                return reportValidation(
                    "non-identity swizzle requires the texture-component-swizzle feature");
        }

        switch (resolved.dimension)
        {
        case WGPUTextureViewDimension_1D:
            if (texture->dimension != WGPUTextureDimension_1D)
                return reportValidation("1d view requires 1d texture");
            if (resolved.arrayLayerCount != 1)
                return reportValidation("1d view arrayLayerCount must be 1");
            break;
        case WGPUTextureViewDimension_2D:
            if (texture->dimension != WGPUTextureDimension_2D)
                return reportValidation("2d view requires 2d texture");
            if (resolved.arrayLayerCount != 1)
                return reportValidation("2d view arrayLayerCount must be 1");
            break;
        case WGPUTextureViewDimension_2DArray:
            if (texture->dimension != WGPUTextureDimension_2D)
                return reportValidation("2d-array view requires 2d texture");
            break;
        case WGPUTextureViewDimension_Cube:
            if (texture->dimension != WGPUTextureDimension_2D)
                return reportValidation("cube view requires 2d texture");
            if (resolved.arrayLayerCount != 6)
                return reportValidation("cube view arrayLayerCount must be 6");
            if (texture->size.width != texture->size.height)
                return reportValidation("cube view requires square texture");
            break;
        case WGPUTextureViewDimension_CubeArray:
            if (texture->dimension != WGPUTextureDimension_2D)
                return reportValidation("cube-array view requires 2d texture");
            if (resolved.arrayLayerCount % 6 != 0)
                return reportValidation("cube-array view arrayLayerCount must be a multiple of 6");
            if (texture->size.width != texture->size.height)
                return reportValidation("cube-array view requires square texture");
            if (!texture->device ||
                wgpuDeviceHasFeature(texture->device,
                                     WGPUFeatureName_CoreFeaturesAndLimits) != WGPU_TRUE)
                return reportValidation(
                    "cube-array view requires the core-features-and-limits feature");
            break;
        case WGPUTextureViewDimension_3D:
            if (texture->dimension != WGPUTextureDimension_3D)
                return reportValidation("3d view requires 3d texture");
            if (resolved.arrayLayerCount != 1)
                return reportValidation("3d view arrayLayerCount must be 1");
            break;
        default:
            break;
        }

        // §23.x: ensure any uninitialized subresources covered by this view become zero
        // before the view is observable. Eager-init at create-time covers fresh textures;
        // this path catches subresources that were marked uninitialized by storeOp=Discard.
        // Skips RENDER_ATTACHMENT-only views (the next render pass with loadOp=Load will
        // do its own pre-pass clear) and STORAGE_BINDING-only views (compute writes
        // overwrite contents anyway).
        if (texture->image &&
            (viewUsage & (WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc)) != 0)
        {
            auto aspects = pwgpu::AspectsForView(texture->format, resolved.aspect);
            pwgpu::LazyInitViewRangeViaQueue(texture,
                                             resolved.baseMipLevel, resolved.mipLevelCount,
                                             resolved.baseArrayLayer, resolved.arrayLayerCount,
                                             aspects);
        }

        auto *view = new WGPUTextureViewImpl();
        wgpuTextureAddRef(texture);
        view->texture = texture;
        view->format = resolved.format;
        view->dimension = resolved.dimension;
        view->usage = viewUsage;
        view->baseMipLevel = resolved.baseMipLevel;
        view->mipLevelCount = resolved.mipLevelCount;
        view->baseArrayLayer = resolved.baseArrayLayer;
        view->arrayLayerCount = resolved.arrayLayerCount;
        view->aspect = resolved.aspect;
        view->swizzle = {sR, sG, sB, sA};
        view->hasNonIdentitySwizzle = !isIdentitySwizzle();
        if (descriptor && descriptor->label.data)
            view->label = pwgpu::ToString(descriptor->label);

        if (viewUsage & WGPUTextureUsage_RenderAttachment)
        {
            uint32_t mipW = std::max(1u, texture->size.width >> resolved.baseMipLevel);
            uint32_t mipH = std::max(1u, texture->size.height >> resolved.baseMipLevel);
            view->renderExtent = {mipW, mipH, 1};
        }

        // Vulkan rejects vkCreateImageView on an image whose usage has no view-usable bits
        // (e.g. a copy-only WebGPU texture). The WebGPU view object is still legal — it just
        // can't be bound to anything — so skip the native view creation in that case.
        constexpr WGPUTextureUsage kVkViewableUsages =
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding |
            WGPUTextureUsage_RenderAttachment;
        if (texture->image && (viewUsage & kVkViewableUsages) != 0)
        {
            VkFormat vkFmt;
            // Combined depth-stencil VkImages aren't created with MUTABLE_FORMAT_BIT,
            // so an aspect-only view (e.g. depth24plus over depth24plus-stencil8)
            // must keep the image's format; aspectMask alone selects the plane.
            const bool combinedDS =
                pwgpu::HasDepthAspect(texture->format) && pwgpu::HasStencilAspect(texture->format);
            if (resolved.format == texture->format || combinedDS)
                vkFmt = static_cast<VkFormat>(pe::ToVkFormat(texture->image->GetFormat()));
            else
                vkFmt = pwgpu::ToVkFormat(resolved.format);
            vk::ImageViewType vkViewType = vk::ImageViewType::e2D;
            switch (resolved.dimension)
            {
            case WGPUTextureViewDimension_1D:
                vkViewType = vk::ImageViewType::e1D;
                break;
            case WGPUTextureViewDimension_2D:
                vkViewType = vk::ImageViewType::e2D;
                break;
            case WGPUTextureViewDimension_2DArray:
                vkViewType = vk::ImageViewType::e2DArray;
                break;
            case WGPUTextureViewDimension_Cube:
                vkViewType = vk::ImageViewType::eCube;
                break;
            case WGPUTextureViewDimension_CubeArray:
                vkViewType = vk::ImageViewType::eCubeArray;
                break;
            case WGPUTextureViewDimension_3D:
                vkViewType = vk::ImageViewType::e3D;
                break;
            default:
                break;
            }

            vk::ImageViewCreateInfo ivci{};
            ivci.image = pe::GetVulkanImage(texture->image);
            ivci.viewType = vkViewType;
            ivci.format = static_cast<vk::Format>(vkFmt);
            ivci.subresourceRange.aspectMask = pwgpu::ToVkAspect(resolved.aspect, resolved.format);
            ivci.subresourceRange.baseMipLevel = resolved.baseMipLevel;
            ivci.subresourceRange.levelCount = resolved.mipLevelCount;
            ivci.subresourceRange.baseArrayLayer = resolved.baseArrayLayer;
            ivci.subresourceRange.layerCount = resolved.arrayLayerCount;

            auto toVkSwizzle = [](WGPUComponentSwizzle s, vk::ComponentSwizzle identity)
            {
                switch (s)
                {
                case WGPUComponentSwizzle_Zero:
                    return vk::ComponentSwizzle::eZero;
                case WGPUComponentSwizzle_One:
                    return vk::ComponentSwizzle::eOne;
                case WGPUComponentSwizzle_R:
                    return vk::ComponentSwizzle::eR;
                case WGPUComponentSwizzle_G:
                    return vk::ComponentSwizzle::eG;
                case WGPUComponentSwizzle_B:
                    return vk::ComponentSwizzle::eB;
                case WGPUComponentSwizzle_A:
                    return vk::ComponentSwizzle::eA;
                default:
                    return identity;
                }
            };
            ivci.components.r = toVkSwizzle(sR, vk::ComponentSwizzle::eR);
            ivci.components.g = toVkSwizzle(sG, vk::ComponentSwizzle::eG);
            ivci.components.b = toVkSwizzle(sB, vk::ComponentSwizzle::eB);
            ivci.components.a = toVkSwizzle(sA, vk::ComponentSwizzle::eA);

            const std::string viewName = view->label.empty()
                                             ? (texture->label + "_view")
                                             : view->label;
            try
            {
                view->view = pe::VulkanImageViewImpl::Create(texture->image, ivci, viewName);
            }
            catch (...)
            {
                view->view = nullptr;
            }

            if (!view->view && texture->device)
            {
                texture->device->reportError(
                    WGPUErrorType_Validation,
                    pwgpu::ToStringView("wgpuTextureCreateView: native image view creation failed"));
            }
        }

        if (view->view)
        {
            std::lock_guard<std::mutex> lock(texture->childViewsMutex);
            texture->childViews.push_back(view);
        }

        return view;
    }

    uint32_t wgpuTextureGetWidth(WGPUTexture texture)
    {
        return texture ? texture->size.width : 0;
    }
    uint32_t wgpuTextureGetHeight(WGPUTexture texture)
    {
        return texture ? texture->size.height : 0;
    }
    uint32_t wgpuTextureGetDepthOrArrayLayers(WGPUTexture texture)
    {
        return texture ? texture->size.depthOrArrayLayers : 0;
    }
    uint32_t wgpuTextureGetMipLevelCount(WGPUTexture texture)
    {
        return texture ? texture->mipLevelCount : 0;
    }
    uint32_t wgpuTextureGetSampleCount(WGPUTexture texture)
    {
        return texture ? texture->sampleCount : 0;
    }
    WGPUTextureFormat wgpuTextureGetFormat(WGPUTexture texture)
    {
        return texture ? texture->format : WGPUTextureFormat_Undefined;
    }
    WGPUTextureDimension wgpuTextureGetDimension(WGPUTexture texture)
    {
        return texture ? texture->dimension : WGPUTextureDimension_2D;
    }
    WGPUTextureUsage wgpuTextureGetUsage(WGPUTexture texture)
    {
        return texture ? texture->usage : WGPUTextureUsage_None;
    }

    WGPUTextureViewDimension wgpuTextureGetTextureBindingViewDimension(WGPUTexture texture)
    {
        if (!texture)
            return WGPUTextureViewDimension_Undefined;
        return texture->textureBindingViewDimension;
    }

    void wgpuTextureSetLabel(WGPUTexture texture, WGPUStringView label)
    {
        if (texture)
            texture->label = pwgpu::ToString(label);
    }

    void wgpuTextureViewAddRef(WGPUTextureView view)
    {
        if (view)
            view->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuTextureViewRelease(WGPUTextureView view)
    {
        if (!view)
            return;
        if (view->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            if (view->texture)
            {
                std::lock_guard<std::mutex> lock(view->texture->childViewsMutex);
                auto &cv = view->texture->childViews;
                cv.erase(std::remove(cv.begin(), cv.end(), view), cv.end());
            }
            if (view->view)
                pe::ImageView::Destroy(view->view);
            if (view->texture)
                wgpuTextureRelease(view->texture);
            delete view;
        }
    }

    void wgpuTextureViewSetLabel(WGPUTextureView view, WGPUStringView label)
    {
        if (view)
            view->label = pwgpu::ToString(label);
    }

} // extern "C"
