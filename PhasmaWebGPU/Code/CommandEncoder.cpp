#include "CommandEncoder.h"
#include "RenderPass.h"
#include "ComputePass.h"
#include "RenderPipeline.h"
#include "ComputePipeline.h"
#include "BindGroup.h"
#include "Buffer.h"
#include "Texture.h"
#include "QuerySet.h"
#include "Device.h"
#include "FormatMap.h"
#include "FormatMap.h"
#include "Utils.h"
#include "API/Image.h"
#include "API/Buffer.h"

extern "C" void wgpuRenderPipelineRelease(WGPURenderPipeline);
extern "C" void wgpuComputePipelineRelease(WGPUComputePipeline);
extern "C" void wgpuBindGroupRelease(WGPUBindGroup);
extern "C" void wgpuQuerySetAddRef(WGPUQuerySet);
extern "C" void wgpuQuerySetRelease(WGPUQuerySet);
extern "C" void wgpuTextureViewRelease(WGPUTextureView);
extern "C" void wgpuRenderBundleRelease(WGPURenderBundle);

void RetainedResources::MergeFrom(RetainedResources &other)
{
    renderPipelines.insert(renderPipelines.end(), other.renderPipelines.begin(), other.renderPipelines.end());
    computePipelines.insert(computePipelines.end(), other.computePipelines.begin(), other.computePipelines.end());
    bindGroups.insert(bindGroups.end(), other.bindGroups.begin(), other.bindGroups.end());
    querySets.insert(querySets.end(), other.querySets.begin(), other.querySets.end());
    textureViews.insert(textureViews.end(), other.textureViews.begin(), other.textureViews.end());
    renderBundles.insert(renderBundles.end(), other.renderBundles.begin(), other.renderBundles.end());
    usedBuffers.insert(usedBuffers.end(), other.usedBuffers.begin(), other.usedBuffers.end());
    usedTextures.insert(usedTextures.end(), other.usedTextures.begin(), other.usedTextures.end());
    other.renderPipelines.clear();
    other.computePipelines.clear();
    other.bindGroups.clear();
    other.querySets.clear();
    other.textureViews.clear();
    other.renderBundles.clear();
    other.usedBuffers.clear();
    other.usedTextures.clear();
}

void RetainedResources::ReleaseAll()
{
    for (auto *p : renderPipelines)
        wgpuRenderPipelineRelease(p);
    for (auto *p : computePipelines)
        wgpuComputePipelineRelease(p);
    for (auto *bg : bindGroups)
        wgpuBindGroupRelease(bg);
    for (auto *qs : querySets)
        wgpuQuerySetRelease(qs);
    for (auto *tv : textureViews)
        wgpuTextureViewRelease(tv);
    for (auto *rb : renderBundles)
        wgpuRenderBundleRelease(rb);
    renderPipelines.clear();
    computePipelines.clear();
    bindGroups.clear();
    querySets.clear();
    textureViews.clear();
    renderBundles.clear();
}

namespace
{
    enum class PassBeginKind
    {
        Render,
        Compute
    };

    const char *PassBeginLabel(PassBeginKind kind)
    {
        switch (kind)
        {
        case PassBeginKind::Render:
            return "beginRenderPass";
        case PassBeginKind::Compute:
            return "beginComputePass";
        default:
            return "beginPass";
        }
    }

    std::string MakePassBeginValidationMessage(PassBeginKind kind, const char *detail)
    {
        std::string message = PassBeginLabel(kind);
        message += ": ";
        message += detail;
        return message;
    }

    std::string ValidatePassTimestampWrites(WGPUDevice device,
                                            WGPUQuerySet querySet,
                                            uint32_t beginningOfPassWriteIndex,
                                            uint32_t endOfPassWriteIndex,
                                            PassBeginKind kind)
    {
        if (!device || wgpuDeviceHasFeature(device, WGPUFeatureName_TimestampQuery) != WGPU_TRUE)
            return MakePassBeginValidationMessage(kind, "timestamp-query feature is not enabled");
        if (!querySet)
            return MakePassBeginValidationMessage(kind, "timestampWrites.querySet is null");
        // §19.2 checks query-set destruction when recorded work is submitted.
        if (querySet->invalid || querySet->queryPool == VK_NULL_HANDLE)
            return MakePassBeginValidationMessage(kind, "timestampWrites.querySet is invalid");
        if (querySet->device != device)
            return MakePassBeginValidationMessage(kind, "timestampWrites.querySet belongs to a different device");
        if (querySet->type != WGPUQueryType_Timestamp)
            return MakePassBeginValidationMessage(kind, "timestampWrites.querySet.type must be timestamp");

        const bool hasBegin = (beginningOfPassWriteIndex != WGPU_QUERY_SET_INDEX_UNDEFINED);
        const bool hasEnd = (endOfPassWriteIndex != WGPU_QUERY_SET_INDEX_UNDEFINED);
        if (!hasBegin && !hasEnd)
            return MakePassBeginValidationMessage(kind, "timestampWrites requires at least one write index");
        if (hasBegin && beginningOfPassWriteIndex >= querySet->count)
            return MakePassBeginValidationMessage(kind, "beginningOfPassWriteIndex >= querySet.count");
        if (hasEnd && endOfPassWriteIndex >= querySet->count)
            return MakePassBeginValidationMessage(kind, "endOfPassWriteIndex >= querySet.count");
        if (hasBegin && hasEnd && beginningOfPassWriteIndex == endOfPassWriteIndex)
            return MakePassBeginValidationMessage(kind,
                                                  "beginningOfPassWriteIndex equals endOfPassWriteIndex");

        return {};
    }

    // §13.7: encoder errors fire at finish(), not at the call site (first wins).
    void ReportEncoderValidation(WGPUCommandEncoder enc, const char *msg)
    {
        if (!enc)
            return;
        if (enc->deferredErrorMessage.empty())
            enc->deferredErrorMessage = msg ? msg : "";
    }

    bool EncoderOpen(WGPUCommandEncoder enc, const char *apiName)
    {
        if (!enc)
            return false;
        if (enc->finished)
        {
            // No future finish() to surface a deferred message — fire directly.
            if (enc->device)
            {
                std::string msg = std::string(apiName) + ": encoder is already finished";
                enc->device->reportError(WGPUErrorType_Validation,
                                         pwgpu::ToStringView(msg.c_str()));
            }
            return false;
        }
        if (enc->hasOpenPass)
        {
            std::string msg = std::string(apiName) + ": a pass is currently open on this encoder";
            ReportEncoderValidation(enc, msg.c_str());
            enc->invalid = true;
            return false;
        }
        return true;
    }

    WGPUExtent3D MipExtent(const WGPUTextureImpl *tex, uint32_t mipLevel)
    {
        uint32_t w = std::max(1u, tex->size.width >> mipLevel);
        uint32_t h = std::max(1u, tex->size.height >> mipLevel);
        uint32_t d = (tex->dimension == WGPUTextureDimension_3D) ? std::max(1u, tex->size.depthOrArrayLayers >> mipLevel) : tex->size.depthOrArrayLayers;
        return {w, h, d};
    }

    // §3.7.7: virtual extent rounded up to block dims (BC mips < block size still admit a single-block copy).
    WGPUExtent3D PhysicalMipExtent(const WGPUTextureImpl *tex, uint32_t mipLevel)
    {
        WGPUExtent3D virt = MipExtent(tex, mipLevel);
        uint32_t blockW = 1, blockH = 1;
        pwgpu::GetTexelBlockSize(tex->format, blockW, blockH);
        if (blockW > 1)
            virt.width = ((virt.width + blockW - 1) / blockW) * blockW;
        if (blockH > 1)
            virt.height = ((virt.height + blockH - 1) / blockH) * blockH;
        return virt;
    }

    vk::ImageAspectFlags AspectForCopy(WGPUTextureAspect aspect, WGPUTextureFormat fmt)
    {
        return pwgpu::ToVkAspect(aspect, fmt);
    }

    bool IsFullMipRegion(const WGPUTextureImpl *tex, uint32_t mipLevel,
                         const WGPUOrigin3D &origin, const WGPUExtent3D &copySize)
    {
        if (mipLevel >= tex->mipLevelCount)
            return false;
        WGPUExtent3D mip = MipExtent(tex, mipLevel);
        if (origin.x != 0 || origin.y != 0 || origin.z != 0)
            return false;
        if (copySize.width != mip.width || copySize.height != mip.height)
            return false;
        uint32_t fullDepth = (tex->dimension == WGPUTextureDimension_3D)
                                 ? mip.depthOrArrayLayers
                                 : tex->size.depthOrArrayLayers;
        return copySize.depthOrArrayLayers == fullDepth;
    }

    bool ValidateTextureCopyRange(const WGPUTextureImpl *tex, uint32_t mipLevel,
                                  const WGPUOrigin3D &origin, const WGPUExtent3D &copySize)
    {
        if (mipLevel >= tex->mipLevelCount)
            return false;
        // Bounds check uses the *physical* mip extent (block-aligned for BC
        // formats) per W3C §3.7.7. Block-alignment of origin and the trailing
        // edge is then enforced relative to that same physical extent: a copy
        // ending exactly at the physical edge is allowed even if the trailing
        // size is not a whole block, because the leftover sliver inside the
        // last block is still a full block of compressed data.
        WGPUExtent3D mip = PhysicalMipExtent(tex, mipLevel);
        uint32_t blockW = 1, blockH = 1;
        pwgpu::GetTexelBlockSize(tex->format, blockW, blockH);
        if (pwgpu::HasDepthAspect(tex->format) || pwgpu::HasStencilAspect(tex->format))
        {
            if (!IsFullMipRegion(tex, mipLevel, origin, copySize))
                return false;
        }
        // Promote to uint64 before summing so that pathological inputs (e.g. a
        // signed -1 reinterpreted as 0xFFFFFFFF, or origin near UINT32_MAX) do
        // not silently wrap and bypass the bounds check.
        uint64_t endX = static_cast<uint64_t>(origin.x) + static_cast<uint64_t>(copySize.width);
        uint64_t endY = static_cast<uint64_t>(origin.y) + static_cast<uint64_t>(copySize.height);
        uint64_t endZ = static_cast<uint64_t>(origin.z) + static_cast<uint64_t>(copySize.depthOrArrayLayers);
        if (endX > mip.width)
            return false;
        if (endY > mip.height)
            return false;
        if (tex->dimension == WGPUTextureDimension_3D)
        {
            if (endZ > mip.depthOrArrayLayers)
                return false;
        }
        else
        {
            if (endZ > tex->size.depthOrArrayLayers)
                return false;
        }
        if (blockW > 1 || blockH > 1)
        {
            if (origin.x % blockW != 0 || origin.y % blockH != 0)
                return false;
            if (endX != mip.width && (copySize.width % blockW) != 0)
                return false;
            if (endY != mip.height && (copySize.height % blockH) != 0)
                return false;
        }
        return true;
    }

    // Mirrors W3C "validating linear texture data" (§7.3.2) and CTS
    // dataBytesForCopyOrOverestimate. Layout-parameter validity (bytesPerRow
    // present when required, rowsPerImage >= heightInBlocks when provided) is
    // checked even for zero-volume copies: the spec does not short-circuit on
    // a zero-sized extent. Overflow is caught by using uint64 throughout and
    // rejecting multiplications whose 128-bit product wouldn't fit.
    bool ValidateBufferCopyLayout(uint64_t bufferSize, uint64_t offset,
                                  uint32_t bytesPerRow, uint32_t rowsPerImage,
                                  const WGPUExtent3D &copySize,
                                  uint32_t footprint, uint32_t blockW, uint32_t blockH)
    {
        if (footprint == 0)
            return false;

        uint32_t widthInBlocks = (copySize.width + blockW - 1) / blockW;
        uint32_t heightInBlocks = (copySize.height + blockH - 1) / blockH;
        uint32_t copyDepth = copySize.depthOrArrayLayers;
        uint64_t bytesInLastRow = static_cast<uint64_t>(widthInBlocks) * footprint;
        bool bprProvided = (bytesPerRow != WGPU_COPY_STRIDE_UNDEFINED);
        bool rpiProvided = (rowsPerImage != WGPU_COPY_STRIDE_UNDEFINED);

        // If bytesPerRow is required (heightInBlocks > 1 OR depth > 1), it must be provided.
        if ((heightInBlocks > 1 || copyDepth > 1) && !bprProvided)
            return false;
        // If rowsPerImage is required (depth > 1), it must be provided.
        if (copyDepth > 1 && !rpiProvided)
            return false;
        // When provided, bytesPerRow must be >= bytesInLastRow.
        if (bprProvided && static_cast<uint64_t>(bytesPerRow) < bytesInLastRow)
            return false;
        // When provided, rowsPerImage must be >= heightInBlocks.
        if (rpiProvided && rowsPerImage < heightInBlocks)
            return false;

        // Offset alone must fit in the buffer even for empty copies.
        if (offset > bufferSize)
            return false;

        // Compute requiredBytesInCopy, detecting overflow.
        auto mulCheck = [](uint64_t a, uint64_t b, uint64_t &out) -> bool
        {
            if (a != 0 && b > (UINT64_MAX / a))
                return false;
            out = a * b;
            return true;
        };
        auto addCheck = [](uint64_t a, uint64_t b, uint64_t &out) -> bool
        {
            if (b > UINT64_MAX - a)
                return false;
            out = a + b;
            return true;
        };

        uint64_t bpr = bprProvided ? static_cast<uint64_t>(bytesPerRow) : bytesInLastRow;
        uint64_t rpi = rpiProvided ? static_cast<uint64_t>(rowsPerImage) : static_cast<uint64_t>(heightInBlocks);

        uint64_t requiredBytesInCopy = 0;
        if (copyDepth > 1)
        {
            uint64_t bytesPerImage = 0;
            if (!mulCheck(bpr, rpi, bytesPerImage))
                return false;
            uint64_t add = 0;
            if (!mulCheck(bytesPerImage, static_cast<uint64_t>(copyDepth - 1), add))
                return false;
            if (!addCheck(requiredBytesInCopy, add, requiredBytesInCopy))
                return false;
        }
        if (copyDepth > 0)
        {
            if (heightInBlocks > 1)
            {
                uint64_t add = 0;
                if (!mulCheck(bpr, static_cast<uint64_t>(heightInBlocks - 1), add))
                    return false;
                if (!addCheck(requiredBytesInCopy, add, requiredBytesInCopy))
                    return false;
            }
            if (heightInBlocks > 0)
            {
                if (!addCheck(requiredBytesInCopy, bytesInLastRow, requiredBytesInCopy))
                    return false;
            }
        }

        uint64_t required = 0;
        if (!addCheck(offset, requiredBytesInCopy, required))
            return false;
        return required <= bufferSize;
    }

    bool IsDSCopyAspectMethodSupported(WGPUTextureFormat fmt, WGPUTextureAspect aspect, bool isT2B)
    {
        bool hasD = pwgpu::HasDepthAspect(fmt);
        bool hasS = pwgpu::HasStencilAspect(fmt);
        if (!hasD && !hasS)
            return true;
        auto depth = (aspect == WGPUTextureAspect_DepthOnly);
        auto sten = (aspect == WGPUTextureAspect_StencilOnly);
        switch (fmt)
        {
        case WGPUTextureFormat_Stencil8:
            return sten || aspect == WGPUTextureAspect_All;
        case WGPUTextureFormat_Depth16Unorm:
            return depth || aspect == WGPUTextureAspect_All;
        case WGPUTextureFormat_Depth32Float:
            return isT2B && (depth || aspect == WGPUTextureAspect_All);
        case WGPUTextureFormat_Depth24Plus:
            return false;
        case WGPUTextureFormat_Depth24PlusStencil8:
            return sten;
        case WGPUTextureFormat_Depth32FloatStencil8:
            return isT2B ? (depth || sten) : sten;
        default:
            return true;
        }
    }

    bool ValidateDSFullMipForCopy(WGPUCommandEncoder enc, const WGPUTextureImpl *tex, uint32_t mipLevel,
                                  const WGPUOrigin3D &origin, const WGPUExtent3D &copySize,
                                  const char *apiName, const char *sideName)
    {
        if (!tex)
            return true;
        if (!(pwgpu::HasDepthAspect(tex->format) || pwgpu::HasStencilAspect(tex->format)))
            return true;
        if (IsFullMipRegion(tex, mipLevel, origin, copySize))
            return true;
        std::string msg = std::string(apiName) + ": " + sideName +
                          " depth-or-stencil copy must span the entire mip (origin==(0,0,0), "
                          "size==mip extent)";
        ReportEncoderValidation(enc, msg.c_str());
        return false;
    }

    bool ValidateDSCopyAspect(WGPUCommandEncoder enc, WGPUTextureFormat fmt, WGPUTextureAspect aspect,
                              bool isT2B, const char *apiName, const char *sideName)
    {
        if (IsDSCopyAspectMethodSupported(fmt, aspect, isT2B))
            return true;
        std::string msg = std::string(apiName) + ": " + sideName +
                          " aspect is not valid for the depth-or-stencil format in this copy direction";
        ReportEncoderValidation(enc, msg.c_str());
        return false;
    }

    bool ValidateT2TDepthStencilAspects(WGPUCommandEncoder enc,
                                        WGPUTextureFormat srcFmt, WGPUTextureAspect srcAspect,
                                        WGPUTextureFormat dstFmt, WGPUTextureAspect dstAspect,
                                        const char *apiName)
    {
        bool srcDS = pwgpu::HasDepthAspect(srcFmt) || pwgpu::HasStencilAspect(srcFmt);
        bool dstDS = pwgpu::HasDepthAspect(dstFmt) || pwgpu::HasStencilAspect(dstFmt);
        if (!srcDS && !dstDS)
            return true;
        auto refersToAllAspects = [](WGPUTextureFormat f, WGPUTextureAspect a) -> bool
        {
            if (!pwgpu::AspectPresentInFormat(a, f))
                return false;
            vk::ImageAspectFlags full = pwgpu::ToVkAspect(WGPUTextureAspect_All, f);
            return pwgpu::ToVkAspect(a, f) == full;
        };
        if (!refersToAllAspects(srcFmt, srcAspect) || !refersToAllAspects(dstFmt, dstAspect))
        {
            std::string msg = std::string(apiName) +
                              ": depth-or-stencil copyTextureToTexture requires source and "
                              "destination aspect to refer to all aspects of the format";
            ReportEncoderValidation(enc, msg.c_str());
            return false;
        }
        return true;
    }

    bool ValidateCopyOffsetAlignment(uint64_t offset, WGPUTextureFormat fmt, WGPUTextureAspect aspect)
    {
        if (pwgpu::HasDepthAspect(fmt) || pwgpu::HasStencilAspect(fmt))
            return (offset % 4) == 0;
        uint32_t footprint = pwgpu::TexelBlockCopyFootprint(fmt, aspect);
        if (footprint == 0)
            return true;
        return (offset % footprint) == 0;
    }
} // namespace

extern "C"
{

    void wgpuCommandEncoderAddRef(WGPUCommandEncoder enc)
    {
        if (enc)
            enc->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuCommandEncoderRelease(WGPUCommandEncoder enc)
    {
        if (!enc)
            return;
        if (enc->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            enc->retained.ReleaseAll();
            if (enc->cmd)
            {
                enc->cmd->End();
                enc->cmd->Return();
            }
            delete enc;
        }
    }

    WGPURenderPassEncoder wgpuCommandEncoderBeginRenderPass(WGPUCommandEncoder enc,
                                                            WGPURenderPassDescriptor const *descriptor)
    {
        if (!enc)
            return nullptr;
        if (enc->finished)
        {
            ReportEncoderValidation(enc, "beginRenderPass: encoder is already finished");
            auto *rpe = new WGPURenderPassEncoderImpl();
            rpe->parent = enc;
            rpe->device = enc->device;
            rpe->invalid = true;
            rpe->wasOpened = false;
            return rpe;
        }
        if (enc->hasOpenPass)
        {
            ReportEncoderValidation(enc, "beginRenderPass: a pass is already open on this encoder");
            enc->invalid = true;
            auto *rpe = new WGPURenderPassEncoderImpl();
            rpe->parent = enc;
            rpe->device = enc->device;
            rpe->invalid = true;
            rpe->wasOpened = false;
            return rpe;
        }
        if (!descriptor)
        {
            ReportEncoderValidation(enc, "beginRenderPass: descriptor is null");
            enc->invalid = true;
            auto *rpe = new WGPURenderPassEncoderImpl();
            rpe->parent = enc;
            rpe->device = enc->device;
            rpe->invalid = true;
            return rpe;
        }

        auto makeInvalidPass = [&](const char *msg = "beginRenderPass: invalid descriptor") -> WGPURenderPassEncoder
        {
            ReportEncoderValidation(enc, msg);
            enc->invalid = true;
            auto *rpe = new WGPURenderPassEncoderImpl();
            rpe->parent = enc;
            rpe->device = enc->device;
            rpe->invalid = true;
            enc->hasOpenPass = true;
            return rpe;
        };

        // Destroyed-resource error must defer to queue.submit() per spec; parent encoder stays valid.
        auto makeDeferredPass = [&](WGPUTextureViewImpl *retainView) -> WGPURenderPassEncoder
        {
            if (retainView)
            {
                retainView->refCount.fetch_add(1, std::memory_order_relaxed);
                enc->retained.textureViews.push_back(retainView);
            }
            auto *rpe = new WGPURenderPassEncoderImpl();
            rpe->parent = enc;
            rpe->device = enc->device;
            rpe->invalid = true;
            rpe->deferredResourceError = true;
            enc->hasOpenPass = true;
            return rpe;
        };

        size_t colorCount = descriptor->colorAttachmentCount;
        uint32_t maxColorAttachments = enc->device ? enc->device->limits.maxColorAttachments : 8;
        if (colorCount > maxColorAttachments)
        {
            ReportEncoderValidation(enc,
                                    "beginRenderPass: colorAttachmentCount exceeds maxColorAttachments");
            enc->invalid = true;
            auto *rpe = new WGPURenderPassEncoderImpl();
            rpe->parent = enc;
            rpe->device = enc->device;
            rpe->invalid = true;
            return rpe;
        }

        if (enc->device && colorCount > 0)
        {
            std::vector<WGPUTextureFormat> fmts;
            fmts.reserve(colorCount);
            for (size_t i = 0; i < colorCount; ++i)
            {
                auto &ca = descriptor->colorAttachments[i];
                fmts.push_back(ca.view ? ca.view->format : WGPUTextureFormat_Undefined);
            }
            uint32_t bps = pwgpu::ComputeBytesPerSampleFromFormats(fmts.data(), fmts.size());
            if (bps > enc->device->limits.maxColorAttachmentBytesPerSample)
            {
                ReportEncoderValidation(enc,
                                        "beginRenderPass: attachments exceed maxColorAttachmentBytesPerSample");
                enc->invalid = true;
                auto *rpe = new WGPURenderPassEncoderImpl();
                rpe->parent = enc;
                rpe->device = enc->device;
                rpe->invalid = true;
                return rpe;
            }
        }

        bool hasAnyColorAttachment = false;
        for (size_t i = 0; i < colorCount; i++)
        {
            if (descriptor->colorAttachments[i].view != nullptr)
            {
                hasAnyColorAttachment = true;
                break;
            }
        }
        if (!hasAnyColorAttachment && !descriptor->depthStencilAttachment)
            return makeInvalidPass("no attachments provided");

        uint32_t attachW = 0, attachH = 0;
        uint32_t commonSampleCount = 0;

        for (size_t i = 0; i < colorCount; i++)
        {
            auto &ca = descriptor->colorAttachments[i];
            if (!ca.view)
                continue;

            auto *view = ca.view;
            if (!view->texture || view->texture->invalid)
                return makeInvalidPass("view has no texture or view is invalid");
            if (view->texture->destroyed)
                return makeDeferredPass(view);
            if (!view->view)
                return makeInvalidPass("view is invalid");
            if (view->texture->device != enc->device)
            {
                ReportEncoderValidation(enc,
                                        "beginRenderPass: attachment texture belongs to a different device");
                enc->invalid = true;
            }
            {
                const bool tier1View =
                    enc->device &&
                    wgpuDeviceHasFeature(enc->device, WGPUFeatureName_TextureFormatsTier1) ==
                        WGPU_TRUE;
                if (pwgpu::IsDepthStencilFormat(view->format) ||
                    (!pwgpu::IsRenderableFormat(view->format) &&
                     !(tier1View && pwgpu::IsRenderableFormatTier1(view->format))))
                    return makeInvalidPass("not renderable format");
            }
            if (!(view->usage & WGPUTextureUsage_RenderAttachment))
            {
                ReportEncoderValidation(enc,
                                        "beginRenderPass: attachment view usage must include RenderAttachment");
                enc->invalid = true;
            }
            if (view->mipLevelCount != 1 || view->arrayLayerCount != 1)
                return makeInvalidPass("mip/layer count != 1");

            if (attachW == 0)
            {
                attachW = view->renderExtent.width;
                attachH = view->renderExtent.height;
            }
            else if (view->renderExtent.width != attachW || view->renderExtent.height != attachH)
                return makeInvalidPass(
                    "beginRenderPass: color attachment renderExtent must match across attachments");

            uint32_t sc = view->texture->sampleCount;
            if (commonSampleCount == 0)
                commonSampleCount = sc;
            else if (sc != commonSampleCount)
                return makeInvalidPass(
                    "beginRenderPass: all attachments must have equal sampleCount");

            if (view->dimension == WGPUTextureViewDimension_3D)
            {
                if (ca.depthSlice == WGPU_DEPTH_SLICE_UNDEFINED)
                    return makeInvalidPass(
                        "beginRenderPass: depthSlice must be provided for 3d attachment view");
                uint32_t mipDepth =
                    std::max(view->texture->size.depthOrArrayLayers >> view->baseMipLevel, 1u);
                if (ca.depthSlice >= mipDepth)
                    return makeInvalidPass(
                        "beginRenderPass: depthSlice must be < depth of attachment mip level");
            }
            else
            {
                if (ca.depthSlice != WGPU_DEPTH_SLICE_UNDEFINED)
                    return makeInvalidPass(
                        "beginRenderPass: depthSlice must not be provided for non-3d attachment view");
            }

            if (ca.resolveTarget)
            {
                auto *rt = ca.resolveTarget;
                if (!rt->texture || !rt->view || rt->texture->invalid)
                    return makeInvalidPass("beginRenderPass: resolveTarget is invalid");
                if (rt->texture->device != enc->device)
                {
                    ReportEncoderValidation(
                        enc, "beginRenderPass: resolveTarget belongs to a different device");
                    enc->invalid = true;
                }
                if (rt->texture->destroyed)
                    return makeDeferredPass(rt);
                if (view->texture->sampleCount <= 1)
                    return makeInvalidPass(
                        "beginRenderPass: resolveTarget requires multisampled source view");
                if (rt->texture->sampleCount != 1)
                    return makeInvalidPass(
                        "beginRenderPass: resolveTarget texture sampleCount must be 1");
                if (rt->format != view->format)
                    return makeInvalidPass(
                        "beginRenderPass: resolveTarget format must equal source view format");
                if (rt->texture->format != view->texture->format)
                    return makeInvalidPass(
                        "beginRenderPass: resolveTarget texture format must equal source texture format");
                {
                    const bool tier1Rs =
                        enc->device && wgpuDeviceHasFeature(
                                           enc->device, WGPUFeatureName_TextureFormatsTier1) ==
                                           WGPU_TRUE;
                    if (!pwgpu::SupportsResolve(view->format) &&
                        !(tier1Rs && pwgpu::SupportsResolveTier1(view->format)))
                        return makeInvalidPass(
                            "beginRenderPass: resolveTarget format does not support resolve");
                }
                if (rt->renderExtent.width != view->renderExtent.width ||
                    rt->renderExtent.height != view->renderExtent.height)
                    return makeInvalidPass(
                        "beginRenderPass: resolveTarget extent must match source view extent");
                if (!(rt->usage & WGPUTextureUsage_RenderAttachment))
                    return makeInvalidPass(
                        "beginRenderPass: resolveTarget usage must include RenderAttachment");
                if (rt->dimension == WGPUTextureViewDimension_3D)
                    return makeInvalidPass(
                        "beginRenderPass: resolveTarget must be a non-3d texture view");
                if (rt->mipLevelCount != 1 || rt->arrayLayerCount != 1)
                    return makeInvalidPass(
                        "beginRenderPass: resolveTarget mipLevelCount and arrayLayerCount must be 1");
            }

            if (ca.loadOp != WGPULoadOp_Clear && ca.loadOp != WGPULoadOp_Load)
                return makeInvalidPass("beginRenderPass: colorAttachment.loadOp must be clear or load");
            if (ca.storeOp != WGPUStoreOp_Store && ca.storeOp != WGPUStoreOp_Discard)
                return makeInvalidPass(
                    "beginRenderPass: colorAttachment.storeOp must be store or discard");
        }

        // Color attachments must not alias; 3D compares depthSlice, 2D compares layer ranges.
        for (size_t i = 0; i < colorCount; i++)
        {
            auto &a = descriptor->colorAttachments[i];
            if (!a.view)
                continue;
            for (size_t j = i + 1; j < colorCount; j++)
            {
                auto &b = descriptor->colorAttachments[j];
                if (!b.view)
                    continue;
                if (a.view->texture != b.view->texture)
                    continue;
                if (a.view->baseMipLevel != b.view->baseMipLevel)
                    continue;
                if (a.view->dimension == WGPUTextureViewDimension_3D &&
                    b.view->dimension == WGPUTextureViewDimension_3D)
                {
                    if (a.depthSlice == b.depthSlice)
                        return makeInvalidPass(
                            "beginRenderPass: color attachments must not alias (equal 3d depthSlice)");
                }
                else
                {
                    uint32_t aEnd = a.view->baseArrayLayer + a.view->arrayLayerCount;
                    uint32_t bEnd = b.view->baseArrayLayer + b.view->arrayLayerCount;
                    if (!(aEnd <= b.view->baseArrayLayer || bEnd <= a.view->baseArrayLayer))
                        return makeInvalidPass(
                            "beginRenderPass: color attachments must not alias (overlapping array layers)");
                }
            }
        }

        auto *dsa = descriptor->depthStencilAttachment;
        if (dsa)
        {
            if (!dsa->view || !dsa->view->texture)
                return makeInvalidPass(
                    "beginRenderPass: depthStencilAttachment.view is invalid");
            if (dsa->view->texture->destroyed)
                return makeDeferredPass(dsa->view);
            if (!dsa->view->view || dsa->view->texture->invalid)
                return makeInvalidPass(
                    "beginRenderPass: depthStencilAttachment.view is invalid");

            auto *dsView = dsa->view;
            if (dsView->texture->device != enc->device)
            {
                ReportEncoderValidation(
                    enc,
                    "beginRenderPass: depthStencilAttachment belongs to a different device");
                enc->invalid = true;
            }
            if (!pwgpu::IsDepthStencilFormat(dsView->format))
                return makeInvalidPass(
                    "beginRenderPass: depthStencilAttachment.view format must be depth-stencil");
            if (!(dsView->usage & WGPUTextureUsage_RenderAttachment))
                return makeInvalidPass(
                    "beginRenderPass: depthStencilAttachment.view usage must include RenderAttachment");
            if (dsView->mipLevelCount != 1 || dsView->arrayLayerCount != 1)
                return makeInvalidPass(
                    "beginRenderPass: depthStencilAttachment.view mipLevelCount and arrayLayerCount must be 1");
            if (dsView->dimension == WGPUTextureViewDimension_3D)
                return makeInvalidPass(
                    "beginRenderPass: depthStencilAttachment.view dimension must not be 3d");

            if (attachW == 0)
            {
                attachW = dsView->renderExtent.width;
                attachH = dsView->renderExtent.height;
            }
            else if (dsView->renderExtent.width != attachW || dsView->renderExtent.height != attachH)
                return makeInvalidPass(
                    "beginRenderPass: depthStencilAttachment extent must match color attachment extent");

            uint32_t sc = dsView->texture->sampleCount;
            if (commonSampleCount == 0)
                commonSampleCount = sc;
            else if (sc != commonSampleCount)
                return makeInvalidPass(
                    "beginRenderPass: depthStencilAttachment sampleCount must equal color attachment sampleCount");

            bool hasDepth = pwgpu::HasDepthAspect(dsView->format);
            bool hasStencil = pwgpu::HasStencilAspect(dsView->format);

            if (hasDepth && !dsa->depthReadOnly)
            {
                if (dsa->depthLoadOp != WGPULoadOp_Clear && dsa->depthLoadOp != WGPULoadOp_Load)
                {
                    ReportEncoderValidation(enc,
                                            "beginRenderPass: depthLoadOp must be provided when format has a depth aspect and depthReadOnly is false");
                    enc->invalid = true;
                }
                if (dsa->depthStoreOp != WGPUStoreOp_Store && dsa->depthStoreOp != WGPUStoreOp_Discard)
                {
                    ReportEncoderValidation(enc,
                                            "beginRenderPass: depthStoreOp must be provided when format has a depth aspect and depthReadOnly is false");
                    enc->invalid = true;
                }
            }
            else
            {
                // No depth aspect or depthReadOnly==true: depthLoadOp/depthStoreOp must not be provided.
                if (dsa->depthLoadOp != WGPULoadOp_Undefined || dsa->depthStoreOp != WGPUStoreOp_Undefined)
                {
                    ReportEncoderValidation(enc,
                                            "beginRenderPass: depthLoadOp/depthStoreOp must not be provided when format has no depth aspect or depthReadOnly is true");
                    enc->invalid = true;
                }
            }
            // If depthLoadOp==clear, depthClearValue must be in [0,1]. NaN (JS undefined sentinel) fails.
            if (hasDepth && dsa->depthLoadOp == WGPULoadOp_Clear &&
                !(dsa->depthClearValue >= 0.0f && dsa->depthClearValue <= 1.0f))
                return makeInvalidPass(
                    "beginRenderPass: depthClearValue must be in [0.0, 1.0] when depthLoadOp is clear");

            if (hasStencil && !dsa->stencilReadOnly)
            {
                if (dsa->stencilLoadOp != WGPULoadOp_Clear && dsa->stencilLoadOp != WGPULoadOp_Load)
                {
                    ReportEncoderValidation(enc,
                                            "beginRenderPass: stencilLoadOp must be provided when format has a stencil aspect and stencilReadOnly is false");
                    enc->invalid = true;
                }
                if (dsa->stencilStoreOp != WGPUStoreOp_Store && dsa->stencilStoreOp != WGPUStoreOp_Discard)
                {
                    ReportEncoderValidation(enc,
                                            "beginRenderPass: stencilStoreOp must be provided when format has a stencil aspect and stencilReadOnly is false");
                    enc->invalid = true;
                }
            }
            else
            {
                if (dsa->stencilLoadOp != WGPULoadOp_Undefined || dsa->stencilStoreOp != WGPUStoreOp_Undefined)
                {
                    ReportEncoderValidation(enc,
                                            "beginRenderPass: stencilLoadOp/stencilStoreOp must not be provided when format has no stencil aspect or stencilReadOnly is true");
                    enc->invalid = true;
                }
            }
        }

        if (descriptor->occlusionQuerySet)
        {
            auto *oqs = descriptor->occlusionQuerySet;
            if (oqs->device != enc->device)
                return makeInvalidPass("beginRenderPass: occlusionQuerySet belongs to a different device");
            if (oqs->invalid || oqs->queryPool == VK_NULL_HANDLE)
                return makeInvalidPass("beginRenderPass: occlusionQuerySet is invalid");
            if (oqs->type != WGPUQueryType_Occlusion)
                return makeInvalidPass("beginRenderPass: occlusionQuerySet.type must be occlusion");
        }

        if (descriptor->timestampWrites)
        {
            auto *tw = descriptor->timestampWrites;
            std::string tsErr = ValidatePassTimestampWrites(enc->device,
                                                            tw->querySet,
                                                            tw->beginningOfPassWriteIndex,
                                                            tw->endOfPassWriteIndex,
                                                            PassBeginKind::Render);
            if (!tsErr.empty())
                return makeInvalidPass(tsErr.c_str());
        }

        auto *rpe = new WGPURenderPassEncoderImpl();
        rpe->cmd = enc->cmd;
        rpe->parent = enc;
        rpe->device = enc->device;
        rpe->attachmentWidth = attachW;
        rpe->attachmentHeight = attachH;
        if (descriptor->label.data)
            rpe->label = pwgpu::ToString(descriptor->label);

        if (dsa)
        {
            rpe->depthReadOnly = dsa->depthReadOnly;
            rpe->stencilReadOnly = dsa->stencilReadOnly;
            rpe->depthStencilFormat = dsa->view->format;
        }

        rpe->sampleCount = commonSampleCount ? commonSampleCount : 1;
        rpe->colorFormats.reserve(colorCount);
        for (size_t i = 0; i < colorCount; i++)
        {
            auto &ca = descriptor->colorAttachments[i];
            rpe->colorFormats.push_back(ca.view ? ca.view->format : WGPUTextureFormat_Undefined);
        }

        for (const WGPUChainedStruct *c = descriptor->nextInChain; c; c = c->next)
        {
            if (c->sType == WGPUSType_RenderPassMaxDrawCount)
            {
                rpe->maxDrawCount = reinterpret_cast<const WGPURenderPassMaxDrawCount *>(c)->maxDrawCount;
                break;
            }
        }

        if (descriptor->occlusionQuerySet)
        {
            rpe->occlusionQuerySet = descriptor->occlusionQuerySet;
            wgpuQuerySetAddRef(descriptor->occlusionQuerySet);
            // Do not reset here: createQuerySet host-resets the pool, and §23.6
            // requires unmodified slots to retain values across submissions.
        }

        if (descriptor->timestampWrites)
        {
            auto *tw = descriptor->timestampWrites;
            rpe->timestampQuerySet = tw->querySet;
            wgpuQuerySetAddRef(tw->querySet);

            if (tw->beginningOfPassWriteIndex != WGPU_QUERY_SET_INDEX_UNDEFINED &&
                tw->beginningOfPassWriteIndex < tw->querySet->count)
            {
                rpe->beginTimestampIndex = tw->beginningOfPassWriteIndex;
                enc->cmd->ApiHandle().writeTimestamp2(
                    vk::PipelineStageFlagBits2::eAllCommands,
                    tw->querySet->queryPool, tw->beginningOfPassWriteIndex);
            }
            if (tw->endOfPassWriteIndex != WGPU_QUERY_SET_INDEX_UNDEFINED &&
                tw->endOfPassWriteIndex < tw->querySet->count)
                rpe->endTimestampIndex = tw->endOfPassWriteIndex;
        }

        std::vector<vk::RenderingAttachmentInfo> colorAttachments;
        std::vector<pe::ImageBarrierInfo> barriers;
        colorAttachments.reserve(colorCount);

        for (size_t i = 0; i < colorCount; i++)
        {
            auto &ca = descriptor->colorAttachments[i];
            if (!ca.view)
            {
                vk::RenderingAttachmentInfo nullAtt{};
                nullAtt.imageView = VK_NULL_HANDLE;
                nullAtt.imageLayout = vk::ImageLayout::eUndefined;
                nullAtt.loadOp = vk::AttachmentLoadOp::eDontCare;
                nullAtt.storeOp = vk::AttachmentStoreOp::eDontCare;
                colorAttachments.push_back(nullAtt);
                continue;
            }

            auto *view = ca.view;

            // For 3D views, create a temporary 2D slice view targeting the specific depthSlice.
            // Vulkan dynamic rendering requires a 2D view; a 3D VkImageView is not valid as a
            // color attachment. The image must have VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT.
            vk::ImageView attachmentImageView = view->view->ApiHandle();
            pe::ImageView *sliceView = nullptr;
            uint32_t barrierBaseLayer = view->baseArrayLayer;

            if (view->dimension == WGPUTextureViewDimension_3D &&
                ca.depthSlice != WGPU_DEPTH_SLICE_UNDEFINED)
            {
                vk::ImageViewCreateInfo sliceIvci = pe::ImageView::CreateInfoInit();
                sliceIvci.image = view->texture->image->ApiHandle();
                sliceIvci.viewType = vk::ImageViewType::e2D;
                sliceIvci.format = static_cast<vk::Format>(pwgpu::ToVkFormat(view->format));
                sliceIvci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
                sliceIvci.subresourceRange.baseMipLevel = view->baseMipLevel;
                sliceIvci.subresourceRange.levelCount = 1;
                sliceIvci.subresourceRange.baseArrayLayer = ca.depthSlice;
                sliceIvci.subresourceRange.layerCount = 1;

                try
                {
                    sliceView = pe::ImageView::Create(view->texture->image, sliceIvci, "wgpu_3d_slice");
                }
                catch (...)
                {
                    PE_WARN("[WebGPU] beginRenderPass: failed to create 2D slice view for 3D attachment %zu", i);
                }

                if (sliceView)
                {
                    attachmentImageView = sliceView->ApiHandle();
                    rpe->ownedSliceViews.push_back(sliceView);
                    // Vulkan 3D images have arrayLayers=1; barrier stays at layer 0.
                }
            }

            pe::ImageBarrierInfo barrier{};
            barrier.image = view->texture->image;
            barrier.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            barrier.accessMask = vk::AccessFlagBits2::eColorAttachmentWrite | vk::AccessFlagBits2::eColorAttachmentRead;
            barrier.layout = vk::ImageLayout::eColorAttachmentOptimal;
            barrier.baseMipLevel = view->baseMipLevel;
            barrier.mipLevels = 1;
            barrier.baseArrayLayer = barrierBaseLayer;
            barrier.arrayLayers = 1;
            barriers.push_back(barrier);

            view->refCount.fetch_add(1, std::memory_order_relaxed);
            rpe->retainedViews.push_back(view);

            {
                std::string err;
                if (view->dimension == WGPUTextureViewDimension_3D)
                {
                    pwgpu::SubresourceKey key{};
                    key.texture = view->texture;
                    key.aspect = static_cast<uint32_t>(WGPUTextureAspect_All);
                    key.mip = view->baseMipLevel;
                    key.layer = ca.depthSlice;
                    if (!rpe->usageScope.AddSubresource(key, pwgpu::SubresourceUsageKind::Attachment, err))
                        rpe->usageScopeValid = false;
                }
                else
                {
                    if (!rpe->usageScope.AddView(view, pwgpu::SubresourceUsageKind::Attachment, err))
                        rpe->usageScopeValid = false;
                }
            }

            vk::RenderingAttachmentInfo att{};
            att.imageView = attachmentImageView;
            att.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            att.loadOp = (ca.loadOp == WGPULoadOp_Clear) ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            att.storeOp = (ca.storeOp == WGPUStoreOp_Store) ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;

            if (ca.loadOp == WGPULoadOp_Clear)
            {
                att.clearValue.color.float32[0] = static_cast<float>(ca.clearValue.r);
                att.clearValue.color.float32[1] = static_cast<float>(ca.clearValue.g);
                att.clearValue.color.float32[2] = static_cast<float>(ca.clearValue.b);
                att.clearValue.color.float32[3] = static_cast<float>(ca.clearValue.a);
            }

            if (ca.resolveTarget)
            {
                auto *rt = ca.resolveTarget;

                pe::ImageBarrierInfo resolveBarrier{};
                resolveBarrier.image = rt->texture->image;
                resolveBarrier.stageFlags = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
                resolveBarrier.accessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
                resolveBarrier.layout = vk::ImageLayout::eColorAttachmentOptimal;
                resolveBarrier.baseMipLevel = rt->baseMipLevel;
                resolveBarrier.mipLevels = 1;
                resolveBarrier.baseArrayLayer = rt->baseArrayLayer;
                resolveBarrier.arrayLayers = rt->arrayLayerCount;
                barriers.push_back(resolveBarrier);

                rt->refCount.fetch_add(1, std::memory_order_relaxed);
                rpe->retainedViews.push_back(rt);

                att.resolveMode = pwgpu::IsBlendableFormat(view->format)
                                      ? vk::ResolveModeFlagBits::eAverage
                                      : vk::ResolveModeFlagBits::eSampleZero;
                att.resolveImageView = rt->view->ApiHandle();
                att.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
            }

            colorAttachments.push_back(att);
        }

        vk::RenderingAttachmentInfo depthAtt{};
        vk::RenderingAttachmentInfo stencilAtt{};
        bool hasDepthAttachment = false;
        bool hasStencilAttachment = false;
        if (dsa)
        {
            auto *dsView = dsa->view;
            bool hasDepth = pwgpu::HasDepthAspect(dsView->format);
            bool hasStencil = pwgpu::HasStencilAspect(dsView->format);

            vk::ImageLayout dsLayout = (dsa->depthReadOnly && dsa->stencilReadOnly)
                                           ? vk::ImageLayout::eDepthStencilReadOnlyOptimal
                                       : (dsa->depthReadOnly && hasStencil)
                                           ? vk::ImageLayout::eDepthReadOnlyStencilAttachmentOptimal
                                       : (dsa->stencilReadOnly && hasDepth)
                                           ? vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal
                                           : vk::ImageLayout::eDepthStencilAttachmentOptimal;

            if (hasDepth && !hasStencil)
                dsLayout = dsa->depthReadOnly ? vk::ImageLayout::eDepthReadOnlyOptimal
                                              : vk::ImageLayout::eDepthAttachmentOptimal;
            else if (hasStencil && !hasDepth)
                dsLayout = dsa->stencilReadOnly ? vk::ImageLayout::eStencilReadOnlyOptimal
                                                : vk::ImageLayout::eStencilAttachmentOptimal;

            pe::ImageBarrierInfo dsBarrier{};
            dsBarrier.image = dsView->texture->image;
            dsBarrier.stageFlags = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            dsBarrier.accessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead;
            if (!dsa->depthReadOnly)
                dsBarrier.accessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            if (!dsa->stencilReadOnly)
                dsBarrier.accessMask |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            dsBarrier.layout = dsLayout;
            dsBarrier.baseMipLevel = dsView->baseMipLevel;
            dsBarrier.mipLevels = 1;
            dsBarrier.baseArrayLayer = dsView->baseArrayLayer;
            dsBarrier.arrayLayers = dsView->arrayLayerCount;
            barriers.push_back(dsBarrier);

            dsView->refCount.fetch_add(1, std::memory_order_relaxed);
            rpe->retainedViews.push_back(dsView);

            {
                auto addAspect = [&](WGPUTextureAspect aspect, bool readOnly)
                {
                    pwgpu::SubresourceKey key{};
                    key.texture = dsView->texture;
                    key.aspect = static_cast<uint32_t>(aspect);
                    auto kind = readOnly ? pwgpu::SubresourceUsageKind::AttachmentReadOnly
                                         : pwgpu::SubresourceUsageKind::Attachment;
                    const uint32_t mipEnd = dsView->baseMipLevel + dsView->mipLevelCount;
                    const uint32_t layerEnd = dsView->baseArrayLayer + dsView->arrayLayerCount;
                    for (uint32_t m = dsView->baseMipLevel; m < mipEnd; ++m)
                    {
                        for (uint32_t l = dsView->baseArrayLayer; l < layerEnd; ++l)
                        {
                            key.mip = m;
                            key.layer = l;
                            std::string err;
                            if (!rpe->usageScope.AddSubresource(key, kind, err))
                                rpe->usageScopeValid = false;
                        }
                    }
                };
                if (hasDepth)
                    addAspect(WGPUTextureAspect_DepthOnly, dsa->depthReadOnly);
                if (hasStencil)
                    addAspect(WGPUTextureAspect_StencilOnly, dsa->stencilReadOnly);
            }

            if (hasDepth)
            {
                hasDepthAttachment = true;
                depthAtt.imageView = dsView->view->ApiHandle();
                depthAtt.imageLayout = dsLayout;

                if (!dsa->depthReadOnly)
                {
                    depthAtt.loadOp = (dsa->depthLoadOp == WGPULoadOp_Clear) ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                    depthAtt.storeOp = (dsa->depthStoreOp == WGPUStoreOp_Store) ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
                }
                else
                {
                    depthAtt.loadOp = vk::AttachmentLoadOp::eLoad;
                    depthAtt.storeOp = vk::AttachmentStoreOp::eNone;
                }

                if (dsa->depthLoadOp == WGPULoadOp_Clear)
                    depthAtt.clearValue.depthStencil.depth = dsa->depthClearValue;
            }

            if (hasStencil)
            {
                hasStencilAttachment = true;
                stencilAtt.imageView = dsView->view->ApiHandle();
                stencilAtt.imageLayout = dsLayout;

                if (!dsa->stencilReadOnly)
                {
                    stencilAtt.loadOp = (dsa->stencilLoadOp == WGPULoadOp_Clear) ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
                    stencilAtt.storeOp = (dsa->stencilStoreOp == WGPUStoreOp_Store) ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
                }
                else
                {
                    stencilAtt.loadOp = vk::AttachmentLoadOp::eLoad;
                    stencilAtt.storeOp = vk::AttachmentStoreOp::eNone;
                }

                if (dsa->stencilLoadOp == WGPULoadOp_Clear)
                    stencilAtt.clearValue.depthStencil.stencil = dsa->stencilClearValue;
            }
        }

        // §23.x lazy initialization: any attachment with loadOp=Load whose subresource
        // is currently uninitialized must be cleared before the pass reads it. Convert
        // those slots from Load to Clear(0) — clearValue stays whatever color we set,
        // because the barrier transitions are unchanged and the pass overwrites anyway.
        // Per-attachment, also mark post-pass state at JS-call time: storeOp=Store marks
        // initialized; storeOp=Discard marks uninitialized; readOnly DS marks initialized.
        for (size_t i = 0; i < colorCount; i++)
        {
            auto &ca = descriptor->colorAttachments[i];
            if (!ca.view || !ca.view->texture || !ca.view->texture->image)
                continue;
            auto *view = ca.view;
            uint32_t baseLayer = (view->dimension == WGPUTextureViewDimension_3D &&
                                  ca.depthSlice != WGPU_DEPTH_SLICE_UNDEFINED)
                                     ? ca.depthSlice
                                     : view->baseArrayLayer;
            uint32_t layerCount = (view->dimension == WGPUTextureViewDimension_3D &&
                                   ca.depthSlice != WGPU_DEPTH_SLICE_UNDEFINED)
                                      ? 1u
                                      : view->arrayLayerCount;
            std::vector<uint8_t> aspects = {pwgpu::kAspectColor};

            // Pre-pass: convert loadOp=Load to Clear(0) when subresource is uninitialized
            // so the discard-then-load test pattern reads zero per spec.
            if (ca.loadOp == WGPULoadOp_Load &&
                pwgpu::RangeHasAnyUninitialized(view->texture, view->baseMipLevel, 1,
                                                baseLayer, layerCount, aspects))
            {
                colorAttachments[i].loadOp = vk::AttachmentLoadOp::eClear;
                colorAttachments[i].clearValue.color.float32[0] = 0.0f;
                colorAttachments[i].clearValue.color.float32[1] = 0.0f;
                colorAttachments[i].clearValue.color.float32[2] = 0.0f;
                colorAttachments[i].clearValue.color.float32[3] = 0.0f;
            }

            // Post-pass: storeOp=Store leaves the subresource initialized (rendering
            // wrote it). storeOp=Discard semantically marks it uninitialized so the
            // next read (sample/copy/load) lazy-clears to zero.
            if (ca.storeOp == WGPUStoreOp_Store)
                pwgpu::MarkRangeInitialized(view->texture, view->baseMipLevel, 1,
                                            baseLayer, layerCount, aspects);
            else if (ca.storeOp == WGPUStoreOp_Discard)
                pwgpu::MarkRangeUninitialized(view->texture, view->baseMipLevel, 1,
                                              baseLayer, layerCount, aspects);
        }

        if (dsa && dsa->view && dsa->view->texture && dsa->view->texture->image)
        {
            auto *dsView = dsa->view;
            const bool hasDepth = pwgpu::HasDepthAspect(dsView->format);
            const bool hasStencil = pwgpu::HasStencilAspect(dsView->format);
            std::vector<uint8_t> dAspect = {pwgpu::kAspectDepth};
            std::vector<uint8_t> sAspect = {pwgpu::kAspectStencil};
            uint32_t bL = dsView->baseArrayLayer;
            uint32_t lC = dsView->arrayLayerCount;
            uint32_t bM = dsView->baseMipLevel;
            uint32_t mC = dsView->mipLevelCount;

            if (hasDepth)
            {
                if (!dsa->depthReadOnly && dsa->depthLoadOp == WGPULoadOp_Load &&
                    pwgpu::RangeHasAnyUninitialized(dsView->texture, bM, mC, bL, lC, dAspect))
                {
                    depthAtt.loadOp = vk::AttachmentLoadOp::eClear;
                    depthAtt.clearValue.depthStencil.depth = 0.0f;
                }
                if (dsa->depthReadOnly ||
                    (dsa->depthStoreOp == WGPUStoreOp_Store && dsa->depthLoadOp != WGPULoadOp_Undefined))
                    pwgpu::MarkRangeInitialized(dsView->texture, bM, mC, bL, lC, dAspect);
                else if (dsa->depthStoreOp == WGPUStoreOp_Discard)
                    pwgpu::MarkRangeUninitialized(dsView->texture, bM, mC, bL, lC, dAspect);
            }
            if (hasStencil)
            {
                if (!dsa->stencilReadOnly && dsa->stencilLoadOp == WGPULoadOp_Load &&
                    pwgpu::RangeHasAnyUninitialized(dsView->texture, bM, mC, bL, lC, sAspect))
                {
                    stencilAtt.loadOp = vk::AttachmentLoadOp::eClear;
                    stencilAtt.clearValue.depthStencil.stencil = 0u;
                }
                if (dsa->stencilReadOnly ||
                    (dsa->stencilStoreOp == WGPUStoreOp_Store && dsa->stencilLoadOp != WGPULoadOp_Undefined))
                    pwgpu::MarkRangeInitialized(dsView->texture, bM, mC, bL, lC, sAspect);
                else if (dsa->stencilStoreOp == WGPUStoreOp_Discard)
                    pwgpu::MarkRangeUninitialized(dsView->texture, bM, mC, bL, lC, sAspect);
            }
        }

        // Attachment barriers are deferred: they fire together with bind-group
        // barriers just before beginRendering, so the whole pass's layout
        // transitions collapse into one vkCmdPipelineBarrier2 call.
        rpe->deferredAttachmentBarriers = std::move(barriers);

        // Vulkan dynamic state persists across begin/endRendering; WebGPU §14 resets at pass start.
        // These commands are valid outside dynamic-rendering scope, so set defaults now — user
        // set* commands issued before the first draw will then override correctly.
        vk::Viewport defaultVp{0.0f, 0.0f,
                               static_cast<float>(attachW), static_cast<float>(attachH),
                               0.0f, 1.0f};
        enc->cmd->ApiHandle().setViewport(0, 1, &defaultVp);

        vk::Rect2D defaultScissor{{0, 0}, {attachW, attachH}};
        enc->cmd->ApiHandle().setScissor(0, 1, &defaultScissor);

        const float zeroBlend[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        enc->cmd->ApiHandle().setBlendConstants(zeroBlend);
        enc->cmd->ApiHandle().setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, 0);

        // Defer beginRendering so bind-group barriers can be emitted outside
        // the Vulkan dynamic-rendering scope on first draw-scope command.
        rpe->deferredColorAttachments = std::move(colorAttachments);
        rpe->deferredDepthAtt = depthAtt;
        rpe->deferredStencilAtt = stencilAtt;
        rpe->deferredHasDepth = hasDepthAttachment;
        rpe->deferredHasStencil = hasStencilAttachment;
        rpe->deferredRenderWidth = attachW;
        rpe->deferredRenderHeight = attachH;

        enc->hasOpenPass = true;
        return rpe;
    }

    WGPUComputePassEncoder wgpuCommandEncoderBeginComputePass(WGPUCommandEncoder enc,
                                                              WGPUComputePassDescriptor const *descriptor)
    {
        if (!enc)
            return nullptr;
        if (enc->finished)
        {
            ReportEncoderValidation(enc, "beginComputePass: encoder is already finished");
            auto *cpe = new WGPUComputePassEncoderImpl();
            cpe->parent = enc;
            cpe->device = enc->device;
            cpe->invalid = true;
            cpe->wasOpened = false;
            return cpe;
        }
        if (enc->hasOpenPass)
        {
            ReportEncoderValidation(enc, "beginComputePass: a pass is already open on this encoder");
            enc->invalid = true;
            auto *cpe = new WGPUComputePassEncoderImpl();
            cpe->parent = enc;
            cpe->device = enc->device;
            cpe->invalid = true;
            cpe->wasOpened = false;
            return cpe;
        }
        auto makeInvalidComputePass = [&](const char *msg) -> WGPUComputePassEncoder
        {
            ReportEncoderValidation(enc, msg);
            enc->invalid = true;
            auto *cpe = new WGPUComputePassEncoderImpl();
            cpe->parent = enc;
            cpe->device = enc->device;
            cpe->invalid = true;
            enc->hasOpenPass = true;
            return cpe;
        };

        if (descriptor && descriptor->timestampWrites)
        {
            auto *tw = descriptor->timestampWrites;
            std::string tsErr = ValidatePassTimestampWrites(enc->device,
                                                            tw->querySet,
                                                            tw->beginningOfPassWriteIndex,
                                                            tw->endOfPassWriteIndex,
                                                            PassBeginKind::Compute);
            if (!tsErr.empty())
                return makeInvalidComputePass(tsErr.c_str());
        }

        auto *cpe = new WGPUComputePassEncoderImpl();
        cpe->cmd = enc->cmd;
        cpe->parent = enc;
        cpe->device = enc->device;
        if (descriptor)
        {
            if (descriptor->label.data)
                cpe->label = pwgpu::ToString(descriptor->label);

            if (descriptor->timestampWrites)
            {
                auto *tw = descriptor->timestampWrites;
                cpe->timestampQuerySet = tw->querySet;
                wgpuQuerySetAddRef(tw->querySet);
                if (tw->beginningOfPassWriteIndex != WGPU_QUERY_SET_INDEX_UNDEFINED &&
                    tw->beginningOfPassWriteIndex < tw->querySet->count)
                {
                    cpe->beginTimestampIndex = tw->beginningOfPassWriteIndex;
                    enc->cmd->ApiHandle().writeTimestamp2(
                        vk::PipelineStageFlagBits2::eAllCommands,
                        tw->querySet->queryPool, tw->beginningOfPassWriteIndex);
                }
                if (tw->endOfPassWriteIndex != WGPU_QUERY_SET_INDEX_UNDEFINED &&
                    tw->endOfPassWriteIndex < tw->querySet->count)
                    cpe->endTimestampIndex = tw->endOfPassWriteIndex;
            }
        }
        enc->hasOpenPass = true;
        return cpe;
    }

    WGPUCommandBuffer wgpuCommandEncoderFinish(WGPUCommandEncoder enc, WGPUCommandBufferDescriptor const *descriptor)
    {
        if (!enc)
            return nullptr;
        if (enc->finished)
        {
            if (enc->device)
                enc->device->reportError(WGPUErrorType_Validation,
                                         pwgpu::ToStringView("CommandEncoder.finish(): encoder is already finished"));
            return nullptr;
        }
        if (enc->hasOpenPass)
        {
            if (enc->deferredErrorMessage.empty())
                enc->deferredErrorMessage = "CommandEncoder.finish(): a pass is still open";
            enc->invalid = true;
        }
        if (enc->debugGroupDepth != 0)
        {
            if (enc->deferredErrorMessage.empty())
                enc->deferredErrorMessage = "CommandEncoder.finish(): debug group stack is not empty";
            enc->invalid = true;
        }
        enc->finished = true;

        if (enc->cmd)
            enc->cmd->End();

        auto *cb = new WGPUCommandBufferImpl();
        cb->device = enc->device;
        cb->cmd = enc->cmd;
        enc->cmd = nullptr;
        cb->invalid = enc->invalid;
        cb->deferredResourceError = enc->deferredResourceError;

        cb->retained.MergeFrom(enc->retained);

        if (descriptor && descriptor->label.data)
            cb->label = pwgpu::ToString(descriptor->label);

        if (enc->invalid && enc->device)
        {
            const std::string &msg = enc->deferredErrorMessage.empty()
                                         ? std::string("CommandEncoder.finish(): encoder is invalid")
                                         : enc->deferredErrorMessage;
            enc->device->reportError(WGPUErrorType_Validation, pwgpu::ToStringView(msg.c_str()));
        }
        return cb;
    }

    void wgpuCommandEncoderCopyBufferToBuffer(WGPUCommandEncoder enc,
                                              WGPUBuffer src, uint64_t srcOffset,
                                              WGPUBuffer dst, uint64_t dstOffset,
                                              uint64_t size)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyBufferToBuffer"))
            return;

        auto fail = [&](const char *msg)
        {
            std::string full = std::string("copyBufferToBuffer: ") + msg;
            ReportEncoderValidation(enc, full.c_str());
            enc->invalid = true;
        };
        auto deferToSubmit = [&]()
        { enc->deferredResourceError = true; };

        if (!src || !dst)
        {
            fail("src or dst buffer is null");
            return;
        }
        if (src->invalid || dst->invalid)
        {
            fail("src or dst buffer is invalid");
            return;
        }
        if (src->internalState == BufferInternalState::Destroyed ||
            dst->internalState == BufferInternalState::Destroyed)
        {
            // Destroyed resources defer to queue.submit() per W3C §3.3.
            deferToSubmit();
            return;
        }
        if (src->device != enc->device || dst->device != enc->device)
        {
            fail("src/dst buffer belongs to a different device");
            return;
        }
        if (src == dst)
        {
            fail("src and dst must be different buffers");
            return;
        }
        if (!(src->usage & WGPUBufferUsage_CopySrc))
        {
            fail("src usage must include COPY_SRC");
            return;
        }
        if (!(dst->usage & WGPUBufferUsage_CopyDst))
        {
            fail("dst usage must include COPY_DST");
            return;
        }
        if (srcOffset > src->size || dstOffset > dst->size)
        {
            fail("offset exceeds buffer size");
            return;
        }

        uint64_t copySize = size;
        if (copySize == WGPU_WHOLE_SIZE)
            copySize = src->size - srcOffset;

        if (srcOffset % 4 != 0 || dstOffset % 4 != 0 || copySize % 4 != 0)
        {
            fail("offsets and size must be multiples of 4");
            return;
        }
        if (copySize > src->size - srcOffset ||
            copySize > dst->size - dstOffset)
        {
            fail("copy range out of bounds");
            return;
        }
        if (!src->peBuffer || !dst->peBuffer)
        {
            enc->retained.usedBuffers.push_back(src);
            enc->retained.usedBuffers.push_back(dst);
            return;
        }

        vk::MemoryBarrier2 mb{};
        mb.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        mb.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
        mb.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        mb.dstAccessMask = vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite;
        enc->cmd->MemoryBarrier(mb);

        enc->cmd->CopyBuffer(src->peBuffer, dst->peBuffer, static_cast<size_t>(copySize),
                             static_cast<size_t>(srcOffset), static_cast<size_t>(dstOffset));

        // Record the transfer accesses so the next barrier against these buffers
        // sources from {Copy, TransferRead/Write}. Without this, a subsequent
        // render/compute pass read inherits stale src state and skips the RAW
        // barrier that flushes this copy's write.
        {
            pe::BufferTrackInfo &srcTrack = src->peBuffer->GetTrackInfo();
            srcTrack.stageMask = vk::PipelineStageFlagBits2::eCopy;
            srcTrack.accessMask = vk::AccessFlagBits2::eTransferRead;
            pe::BufferTrackInfo &dstTrack = dst->peBuffer->GetTrackInfo();
            dstTrack.stageMask = vk::PipelineStageFlagBits2::eCopy;
            dstTrack.accessMask = vk::AccessFlagBits2::eTransferWrite;
        }

        enc->retained.usedBuffers.push_back(src);
        enc->retained.usedBuffers.push_back(dst);
    }

    void wgpuCommandEncoderClearBuffer(WGPUCommandEncoder enc,
                                       WGPUBuffer buffer,
                                       uint64_t offset,
                                       uint64_t size)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderClearBuffer"))
            return;
        auto fail = [&](const char *msg)
        {
            std::string full = std::string("clearBuffer: ") + msg;
            ReportEncoderValidation(enc, full.c_str());
            enc->invalid = true;
        };
        auto deferToSubmit = [&]()
        { enc->deferredResourceError = true; };
        if (!buffer)
        {
            fail("buffer is null");
            return;
        }
        if (buffer->invalid)
        {
            fail("buffer is invalid");
            return;
        }
        if (buffer->internalState == BufferInternalState::Destroyed)
        {
            // Destroyed resources defer to queue.submit() per W3C §3.3.
            deferToSubmit();
            return;
        }
        if (buffer->device != enc->device)
        {
            fail("buffer belongs to a different device");
            return;
        }
        if (!(buffer->usage & WGPUBufferUsage_CopyDst))
        {
            fail("buffer usage must include COPY_DST");
            return;
        }

        if (offset % 4 != 0)
        {
            fail("offset must be a multiple of 4");
            return;
        }

        uint64_t clearSize = size;
        if (clearSize == WGPU_WHOLE_SIZE)
        {
            if (offset > buffer->size)
            {
                fail("offset exceeds buffer size");
                return;
            }
            clearSize = buffer->size - offset;
        }

        if (clearSize % 4 != 0)
        {
            fail("size must be a multiple of 4");
            return;
        }
        if (clearSize > buffer->size || offset > buffer->size - clearSize)
        {
            fail("range out of bounds");
            return;
        }

        if (buffer->internalState == BufferInternalState::Destroyed || !buffer->peBuffer)
        {
            enc->retained.usedBuffers.push_back(buffer);
            return;
        }

        if (clearSize == 0)
        {
            enc->retained.usedBuffers.push_back(buffer);
            return;
        }

        enc->cmd->FillBuffer(buffer->peBuffer, static_cast<size_t>(offset),
                             static_cast<size_t>(clearSize), 0);
        enc->retained.usedBuffers.push_back(buffer);
    }

    void wgpuCommandEncoderCopyBufferToTexture(WGPUCommandEncoder enc,
                                               WGPUTexelCopyBufferInfo const *src,
                                               WGPUTexelCopyTextureInfo const *dst,
                                               WGPUExtent3D const *copySize)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyBufferToTexture"))
            return;
        auto fail = [&](const char *msg)
        {
            std::string full = std::string("copyBufferToTexture: ") + msg;
            ReportEncoderValidation(enc, full.c_str());
            enc->invalid = true;
        };
        auto deferToSubmit = [&]()
        { enc->deferredResourceError = true; };
        if (!src || !src->buffer || !copySize || !dst || !dst->texture)
        {
            fail("null descriptor or missing src/dst");
            return;
        }
        if (src->buffer->invalid)
        {
            fail("src buffer is invalid");
            return;
        }
        if (dst->texture->invalid)
        {
            fail("dst texture is invalid");
            return;
        }
        if (src->buffer->internalState == BufferInternalState::Destroyed)
        {
            // Destroyed resources defer to queue.submit() per W3C §3.3.
            deferToSubmit();
            return;
        }
        if (dst->texture->destroyed)
        {
            // Destroyed resources defer to queue.submit() per W3C §3.3.
            deferToSubmit();
            return;
        }
        if (src->buffer->device != enc->device || dst->texture->device != enc->device)
        {
            fail("src/dst belongs to a different device");
            return;
        }
        if (!dst->texture->image || !src->buffer->peBuffer)
        {
            enc->retained.usedBuffers.push_back(src->buffer);
            enc->retained.usedTextures.push_back(dst->texture);
            return;
        }
        if (!(src->buffer->usage & WGPUBufferUsage_CopySrc))
        {
            fail("src buffer usage must include COPY_SRC");
            return;
        }
        if (!(dst->texture->usage & WGPUTextureUsage_CopyDst))
        {
            fail("dst texture usage must include COPY_DST");
            return;
        }

        {
            uint32_t bpr = src->layout.bytesPerRow;
            uint32_t bw, bh;
            pwgpu::GetTexelBlockSize(dst->texture->format, bw, bh);
            uint32_t heightInBlocks = (copySize->height + bh - 1) / bh;
            uint32_t d = copySize->depthOrArrayLayers;
            bool bprRequired = (heightInBlocks > 1) || (d > 1);
            if (bpr == WGPU_COPY_STRIDE_UNDEFINED)
            {
                if (bprRequired)
                {
                    fail("bytesPerRow is required but not provided");
                    return;
                }
            }
            else if (bpr % 256 != 0)
            {
                fail("bytesPerRow must be a multiple of 256");
                return;
            }
        }
        if (!ValidateCopyOffsetAlignment(src->layout.offset, dst->texture->format, dst->aspect))
        {
            fail("layout.offset alignment is invalid for the texture format");
            return;
        }
        if (!ValidateDSCopyAspect(enc, dst->texture->format, dst->aspect, false,
                                  "wgpuCommandEncoderCopyBufferToTexture", "destination"))
        {
            fail("dst aspect is not valid for depth/stencil copy");
            return;
        }
        if (dst->texture->sampleCount > 1)
        {
            fail("dst texture must be single-sampled");
            return;
        }
        if (!ValidateDSFullMipForCopy(enc, dst->texture, dst->mipLevel, dst->origin, *copySize,
                                      "wgpuCommandEncoderCopyBufferToTexture", "destination"))
        {
            fail("dst DS copy must span the entire mip");
            return;
        }
        if (!ValidateTextureCopyRange(dst->texture, dst->mipLevel, dst->origin, *copySize))
        {
            fail("dst copy range out of bounds");
            return;
        }

        WGPUTextureAspect aspect = dst->aspect;
        vk::ImageAspectFlags vkAspect = AspectForCopy(aspect, dst->texture->format);

        uint32_t blockW, blockH;
        pwgpu::GetTexelBlockSize(dst->texture->format, blockW, blockH);
        uint32_t footprint = pwgpu::TexelBlockCopyFootprint(dst->texture->format, aspect);

        if (!ValidateBufferCopyLayout(src->buffer->size, src->layout.offset,
                                      src->layout.bytesPerRow, src->layout.rowsPerImage,
                                      *copySize, footprint, blockW, blockH))
        {
            fail("src buffer layout out of bounds");
            return;
        }

        if (copySize->width == 0 || copySize->height == 0 || copySize->depthOrArrayLayers == 0)
        {
            enc->retained.usedBuffers.push_back(src->buffer);
            enc->retained.usedTextures.push_back(dst->texture);
            return;
        }

        vk::BufferImageCopy2 region{};
        region.bufferOffset = src->layout.offset;
        region.bufferRowLength = (footprint > 0 && src->layout.bytesPerRow != WGPU_COPY_STRIDE_UNDEFINED)
                                     ? (src->layout.bytesPerRow / footprint) * blockW
                                     : 0;
        region.bufferImageHeight = (src->layout.rowsPerImage != WGPU_COPY_STRIDE_UNDEFINED)
                                       ? src->layout.rowsPerImage * blockH
                                       : 0;
        region.imageSubresource.aspectMask = vkAspect;
        region.imageSubresource.mipLevel = dst->mipLevel;
        region.imageSubresource.baseArrayLayer = (dst->texture->dimension == WGPUTextureDimension_3D) ? 0 : dst->origin.z;
        region.imageSubresource.layerCount = (dst->texture->dimension == WGPUTextureDimension_3D) ? 1 : copySize->depthOrArrayLayers;
        region.imageOffset = vk::Offset3D{static_cast<int32_t>(dst->origin.x), static_cast<int32_t>(dst->origin.y),
                                          (dst->texture->dimension == WGPUTextureDimension_3D) ? static_cast<int32_t>(dst->origin.z) : 0};
        region.imageExtent = vk::Extent3D{copySize->width, copySize->height,
                                          (dst->texture->dimension == WGPUTextureDimension_3D) ? copySize->depthOrArrayLayers : 1};

        // §23.x: if dst subresource is uninitialized AND copy doesn't fully cover the
        // mip, lazy-clear it before the partial write so unwritten texels read as zero.
        // After the copy: if full coverage, mark dst initialized.
        const bool dstFullCoverage = IsFullMipRegion(dst->texture, dst->mipLevel, dst->origin, *copySize);
        auto dstAspects = pwgpu::AspectsForView(dst->texture->format, dst->aspect);
        const uint32_t dstBaseLayer = (dst->texture->dimension == WGPUTextureDimension_3D) ? 0u : dst->origin.z;
        const uint32_t dstLayerCount = (dst->texture->dimension == WGPUTextureDimension_3D) ? 1u : copySize->depthOrArrayLayers;
        if (!dstFullCoverage &&
            pwgpu::RangeHasAnyUninitialized(dst->texture, dst->mipLevel, 1, dstBaseLayer, dstLayerCount, dstAspects))
        {
            pwgpu::LazyInitViewRangeOnEncoder(enc->cmd, dst->texture, dst->mipLevel, 1,
                                              dstBaseLayer, dstLayerCount, dstAspects);
        }

        vk::MemoryBarrier2 mb{};
        mb.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        mb.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
        mb.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        mb.dstAccessMask = vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite;
        enc->cmd->MemoryBarrier(mb);

        pe::ImageBarrierInfo barrier{};
        barrier.image = dst->texture->image;
        barrier.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barrier.accessMask = vk::AccessFlagBits2::eTransferWrite;
        barrier.layout = vk::ImageLayout::eTransferDstOptimal;
        barrier.baseMipLevel = dst->mipLevel;
        barrier.mipLevels = 1;
        barrier.baseArrayLayer = region.imageSubresource.baseArrayLayer;
        barrier.arrayLayers = region.imageSubresource.layerCount;
        enc->cmd->ImageBarrier(barrier);

        vk::CopyBufferToImageInfo2 copyInfo{};
        copyInfo.srcBuffer = src->buffer->peBuffer->ApiHandle();
        copyInfo.dstImage = dst->texture->image->ApiHandle();
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        enc->cmd->ApiHandle().copyBufferToImage2(copyInfo);
        if (src->buffer)
            enc->retained.usedBuffers.push_back(src->buffer);
        if (dst->texture)
            enc->retained.usedTextures.push_back(dst->texture);
    }

    void wgpuCommandEncoderCopyTextureToBuffer(WGPUCommandEncoder enc,
                                               WGPUTexelCopyTextureInfo const *src,
                                               WGPUTexelCopyBufferInfo const *dst,
                                               WGPUExtent3D const *copySize)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyTextureToBuffer"))
            return;
        auto fail = [&](const char *msg)
        {
            std::string full = std::string("copyTextureToBuffer: ") + msg;
            ReportEncoderValidation(enc, full.c_str());
            enc->invalid = true;
        };
        auto deferToSubmit = [&]()
        { enc->deferredResourceError = true; };
        if (!src || !src->texture || !copySize || !dst || !dst->buffer)
        {
            fail("null descriptor or missing src/dst");
            return;
        }
        if (src->texture->invalid)
        {
            fail("src texture is invalid");
            return;
        }
        if (dst->buffer->invalid)
        {
            fail("dst buffer is invalid");
            return;
        }
        if (src->texture->destroyed)
        {
            // Destroyed resources defer to queue.submit() per W3C §3.3.
            deferToSubmit();
            return;
        }
        if (dst->buffer->internalState == BufferInternalState::Destroyed)
        {
            // Destroyed resources defer to queue.submit() per W3C §3.3.
            deferToSubmit();
            return;
        }
        if (src->texture->device != enc->device || dst->buffer->device != enc->device)
        {
            fail("src/dst belongs to a different device");
            return;
        }
        if (!src->texture->image || !dst->buffer->peBuffer)
        {
            enc->retained.usedBuffers.push_back(dst->buffer);
            enc->retained.usedTextures.push_back(src->texture);
            return;
        }
        if (!(src->texture->usage & WGPUTextureUsage_CopySrc))
        {
            fail("src texture usage must include COPY_SRC");
            return;
        }
        if (!(dst->buffer->usage & WGPUBufferUsage_CopyDst))
        {
            fail("dst buffer usage must include COPY_DST");
            return;
        }

        {
            uint32_t bpr = dst->layout.bytesPerRow;
            uint32_t bw, bh;
            pwgpu::GetTexelBlockSize(src->texture->format, bw, bh);
            uint32_t heightInBlocks = (copySize->height + bh - 1) / bh;
            uint32_t d = copySize->depthOrArrayLayers;
            bool bprRequired = (heightInBlocks > 1) || (d > 1);
            if (bpr == WGPU_COPY_STRIDE_UNDEFINED)
            {
                if (bprRequired)
                {
                    fail("bytesPerRow is required but not provided");
                    return;
                }
            }
            else if (bpr % 256 != 0)
            {
                fail("bytesPerRow must be a multiple of 256");
                return;
            }
        }
        if (!ValidateCopyOffsetAlignment(dst->layout.offset, src->texture->format, src->aspect))
        {
            fail("layout.offset alignment is invalid for the texture format");
            return;
        }
        if (!ValidateDSCopyAspect(enc, src->texture->format, src->aspect, true,
                                  "wgpuCommandEncoderCopyTextureToBuffer", "source"))
        {
            fail("src aspect is not valid for depth/stencil copy");
            return;
        }
        if (src->texture->sampleCount > 1)
        {
            fail("src texture must be single-sampled");
            return;
        }

        if (!ValidateDSFullMipForCopy(enc, src->texture, src->mipLevel, src->origin, *copySize,
                                      "wgpuCommandEncoderCopyTextureToBuffer", "source"))
        {
            fail("src DS copy must span the entire mip");
            return;
        }
        if (!ValidateTextureCopyRange(src->texture, src->mipLevel, src->origin, *copySize))
        {
            fail("src copy range out of bounds");
            return;
        }

        vk::ImageAspectFlags vkAspect = AspectForCopy(src->aspect, src->texture->format);

        uint32_t blockW, blockH;
        pwgpu::GetTexelBlockSize(src->texture->format, blockW, blockH);
        uint32_t footprint = pwgpu::TexelBlockCopyFootprint(src->texture->format, src->aspect);

        if (!ValidateBufferCopyLayout(dst->buffer->size, dst->layout.offset,
                                      dst->layout.bytesPerRow, dst->layout.rowsPerImage,
                                      *copySize, footprint, blockW, blockH))
        {
            fail("dst buffer layout out of bounds");
            return;
        }

        if (copySize->width == 0 || copySize->height == 0 || copySize->depthOrArrayLayers == 0)
        {
            enc->retained.usedBuffers.push_back(dst->buffer);
            enc->retained.usedTextures.push_back(src->texture);
            return;
        }

        // §23.x: lazy-init any uninitialized source subresources before reading via copy.
        {
            uint32_t srcBaseLayer =
                (src->texture->dimension == WGPUTextureDimension_3D) ? 0u : src->origin.z;
            uint32_t srcLayerCount =
                (src->texture->dimension == WGPUTextureDimension_3D) ? 1u : copySize->depthOrArrayLayers;
            auto srcAspects = pwgpu::AspectsForView(src->texture->format, src->aspect);
            pwgpu::LazyInitViewRangeOnEncoder(enc->cmd, src->texture, src->mipLevel, 1,
                                              srcBaseLayer, srcLayerCount, srcAspects);
        }

        vk::BufferImageCopy2 region{};
        region.bufferOffset = dst->layout.offset;
        region.bufferRowLength = (footprint > 0 && dst->layout.bytesPerRow != WGPU_COPY_STRIDE_UNDEFINED)
                                     ? (dst->layout.bytesPerRow / footprint) * blockW
                                     : 0;
        region.bufferImageHeight = (dst->layout.rowsPerImage != WGPU_COPY_STRIDE_UNDEFINED)
                                       ? dst->layout.rowsPerImage * blockH
                                       : 0;
        region.imageSubresource.aspectMask = vkAspect;
        region.imageSubresource.mipLevel = src->mipLevel;
        region.imageSubresource.baseArrayLayer = (src->texture->dimension == WGPUTextureDimension_3D) ? 0 : src->origin.z;
        region.imageSubresource.layerCount = (src->texture->dimension == WGPUTextureDimension_3D) ? 1 : copySize->depthOrArrayLayers;
        region.imageOffset = vk::Offset3D{static_cast<int32_t>(src->origin.x), static_cast<int32_t>(src->origin.y),
                                          (src->texture->dimension == WGPUTextureDimension_3D) ? static_cast<int32_t>(src->origin.z) : 0};
        region.imageExtent = vk::Extent3D{copySize->width, copySize->height,
                                          (src->texture->dimension == WGPUTextureDimension_3D) ? copySize->depthOrArrayLayers : 1};

        pe::ImageBarrierInfo imgBarrier{};
        imgBarrier.image = src->texture->image;
        imgBarrier.stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        imgBarrier.accessMask = vk::AccessFlagBits2::eTransferRead;
        imgBarrier.layout = vk::ImageLayout::eTransferSrcOptimal;
        imgBarrier.baseMipLevel = src->mipLevel;
        imgBarrier.mipLevels = 1;
        imgBarrier.baseArrayLayer = region.imageSubresource.baseArrayLayer;
        imgBarrier.arrayLayers = region.imageSubresource.layerCount;
        enc->cmd->ImageBarrier(imgBarrier);

        pe::BufferBarrierInfo bufBarrier{};
        bufBarrier.buffer = dst->buffer->peBuffer;
        bufBarrier.stageMask = vk::PipelineStageFlagBits2::eTransfer;
        bufBarrier.accessMask = vk::AccessFlagBits2::eTransferWrite;
        enc->cmd->BufferBarrier(bufBarrier);

        vk::CopyImageToBufferInfo2 copyInfo{};
        copyInfo.srcImage = src->texture->image->ApiHandle();
        copyInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        copyInfo.dstBuffer = dst->buffer->peBuffer->ApiHandle();
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        enc->cmd->ApiHandle().copyImageToBuffer2(copyInfo);
        if (dst->buffer)
            enc->retained.usedBuffers.push_back(dst->buffer);
        if (src->texture)
            enc->retained.usedTextures.push_back(src->texture);
    }

    void wgpuCommandEncoderCopyTextureToTexture(WGPUCommandEncoder enc,
                                                WGPUTexelCopyTextureInfo const *src,
                                                WGPUTexelCopyTextureInfo const *dst,
                                                WGPUExtent3D const *copySize)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderCopyTextureToTexture"))
            return;
        auto fail = [&](const char *msg)
        {
            std::string full = std::string("copyTextureToTexture: ") + msg;
            ReportEncoderValidation(enc, full.c_str());
            enc->invalid = true;
        };
        auto deferToSubmit = [&]()
        { enc->deferredResourceError = true; };
        if (!src || !src->texture || !dst || !dst->texture || !copySize)
        {
            fail("null descriptor or missing src/dst");
            return;
        }
        if (src->texture->invalid || dst->texture->invalid)
        {
            fail("src or dst texture is invalid");
            return;
        }
        if (src->texture->destroyed || dst->texture->destroyed)
        {
            // Destroyed resources defer to queue.submit() per W3C §3.3.
            deferToSubmit();
            return;
        }
        if (src->texture->device != enc->device || dst->texture->device != enc->device)
        {
            fail("src/dst belongs to a different device");
            return;
        }
        if (!src->texture->image || !dst->texture->image)
        {
            enc->retained.usedTextures.push_back(src->texture);
            enc->retained.usedTextures.push_back(dst->texture);
            return;
        }
        if (!(src->texture->usage & WGPUTextureUsage_CopySrc))
        {
            fail("src texture usage must include COPY_SRC");
            return;
        }
        if (!(dst->texture->usage & WGPUTextureUsage_CopyDst))
        {
            fail("dst texture usage must include COPY_DST");
            return;
        }

        if (src->texture->sampleCount != dst->texture->sampleCount)
        {
            fail("sampleCount mismatch between src and dst");
            return;
        }
        if (src->texture->dimension != dst->texture->dimension)
        {
            fail("dimension mismatch between src and dst");
            return;
        }

        if (pwgpu::HasDepthAspect(src->texture->format) || pwgpu::HasStencilAspect(src->texture->format) ||
            pwgpu::HasDepthAspect(dst->texture->format) || pwgpu::HasStencilAspect(dst->texture->format))
        {
            if (src->texture->format != dst->texture->format)
            {
                fail("depth/stencil copy requires identical formats");
                return;
            }
        }
        else if (!pwgpu::AreViewFormatCompatible(src->texture->format, dst->texture->format))
        {
            fail("src/dst formats are not view-compatible");
            return;
        }

        if (!ValidateT2TDepthStencilAspects(enc, src->texture->format, src->aspect,
                                            dst->texture->format, dst->aspect,
                                            "wgpuCommandEncoderCopyTextureToTexture"))
        {
            fail("src/dst aspects must cover all aspects of the format");
            return;
        }
        {
            auto selectsFullAspect = [](WGPUTextureAspect a, WGPUTextureFormat f) -> bool
            {
                vk::ImageAspectFlags full = pwgpu::ToVkAspect(WGPUTextureAspect_All, f);
                if (!pwgpu::AspectPresentInFormat(a, f))
                    return false;
                return pwgpu::ToVkAspect(a, f) == full;
            };
            if (!selectsFullAspect(src->aspect, src->texture->format) ||
                !selectsFullAspect(dst->aspect, dst->texture->format))
            {
                fail("src/dst aspect must select all aspects of the format");
                return;
            }
        }
        if (!ValidateDSFullMipForCopy(enc, src->texture, src->mipLevel, src->origin, *copySize,
                                      "wgpuCommandEncoderCopyTextureToTexture", "source"))
        {
            fail("src DS copy must span the entire mip");
            return;
        }
        if (!ValidateDSFullMipForCopy(enc, dst->texture, dst->mipLevel, dst->origin, *copySize,
                                      "wgpuCommandEncoderCopyTextureToTexture", "destination"))
        {
            fail("dst DS copy must span the entire mip");
            return;
        }

        if (src->texture->sampleCount > 1)
        {
            WGPUExtent3D smip = MipExtent(src->texture, src->mipLevel);
            WGPUExtent3D dmip = MipExtent(dst->texture, dst->mipLevel);
            if (src->origin.x != 0 || src->origin.y != 0 ||
                dst->origin.x != 0 || dst->origin.y != 0 ||
                copySize->width != smip.width || copySize->height != smip.height ||
                copySize->width != dmip.width || copySize->height != dmip.height)
            {
                fail("multisampled copy must span the entire mip");
                return;
            }
        }

        if (!ValidateTextureCopyRange(src->texture, src->mipLevel, src->origin, *copySize))
        {
            fail("src copy range out of bounds");
            return;
        }
        if (!ValidateTextureCopyRange(dst->texture, dst->mipLevel, dst->origin, *copySize))
        {
            fail("dst copy range out of bounds");
            return;
        }

        if (src->texture == dst->texture)
        {
            if (src->mipLevel == dst->mipLevel)
            {
                uint32_t srcLayer0 = (src->texture->dimension == WGPUTextureDimension_3D) ? 0u : src->origin.z;
                uint32_t dstLayer0 = (dst->texture->dimension == WGPUTextureDimension_3D) ? 0u : dst->origin.z;
                uint32_t layerCount = (src->texture->dimension == WGPUTextureDimension_3D) ? 1u : copySize->depthOrArrayLayers;
                uint32_t srcEnd = srcLayer0 + layerCount;
                uint32_t dstEnd = dstLayer0 + layerCount;
                bool disjoint = (srcEnd <= dstLayer0) || (dstEnd <= srcLayer0);
                if (!disjoint)
                {
                    fail("intra-texture copy subresources overlap");
                    return;
                }
            }
        }

        vk::ImageAspectFlags srcAspect = AspectForCopy(src->aspect, src->texture->format);
        vk::ImageAspectFlags dstAspect = AspectForCopy(dst->aspect, dst->texture->format);

        if (copySize->width == 0 || copySize->height == 0 || copySize->depthOrArrayLayers == 0)
        {
            enc->retained.usedTextures.push_back(src->texture);
            enc->retained.usedTextures.push_back(dst->texture);
            return;
        }

        bool src3D = (src->texture->dimension == WGPUTextureDimension_3D);
        bool dst3D = (dst->texture->dimension == WGPUTextureDimension_3D);

        // §23.x: lazy-init any uninitialized source subresources before reading via copy.
        {
            uint32_t srcBaseLayer = src3D ? 0u : src->origin.z;
            uint32_t srcLayerCount = src3D ? 1u : copySize->depthOrArrayLayers;
            auto srcAspects = pwgpu::AspectsForView(src->texture->format, src->aspect);
            pwgpu::LazyInitViewRangeOnEncoder(enc->cmd, src->texture, src->mipLevel, 1,
                                              srcBaseLayer, srcLayerCount, srcAspects);
        }

        vk::ImageCopy2 region{};
        region.srcSubresource.aspectMask = srcAspect;
        region.srcSubresource.mipLevel = src->mipLevel;
        region.srcSubresource.baseArrayLayer = src3D ? 0 : src->origin.z;
        region.srcSubresource.layerCount = src3D ? 1 : copySize->depthOrArrayLayers;
        region.srcOffset = vk::Offset3D{static_cast<int32_t>(src->origin.x), static_cast<int32_t>(src->origin.y),
                                        src3D ? static_cast<int32_t>(src->origin.z) : 0};
        region.dstSubresource.aspectMask = dstAspect;
        region.dstSubresource.mipLevel = dst->mipLevel;
        region.dstSubresource.baseArrayLayer = dst3D ? 0 : dst->origin.z;
        region.dstSubresource.layerCount = dst3D ? 1 : copySize->depthOrArrayLayers;
        region.dstOffset = vk::Offset3D{static_cast<int32_t>(dst->origin.x), static_cast<int32_t>(dst->origin.y),
                                        dst3D ? static_cast<int32_t>(dst->origin.z) : 0};
        region.extent = vk::Extent3D{copySize->width, copySize->height,
                                     (src3D || dst3D) ? copySize->depthOrArrayLayers : 1};

        std::vector<pe::ImageBarrierInfo> barriers(2);
        barriers[0].image = src->texture->image;
        barriers[0].stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barriers[0].accessMask = vk::AccessFlagBits2::eTransferRead;
        barriers[0].layout = vk::ImageLayout::eTransferSrcOptimal;
        barriers[0].baseMipLevel = src->mipLevel;
        barriers[0].mipLevels = 1;
        barriers[0].baseArrayLayer = region.srcSubresource.baseArrayLayer;
        barriers[0].arrayLayers = region.srcSubresource.layerCount;

        barriers[1].image = dst->texture->image;
        barriers[1].stageFlags = vk::PipelineStageFlagBits2::eTransfer;
        barriers[1].accessMask = vk::AccessFlagBits2::eTransferWrite;
        barriers[1].layout = vk::ImageLayout::eTransferDstOptimal;
        barriers[1].baseMipLevel = dst->mipLevel;
        barriers[1].mipLevels = 1;
        barriers[1].baseArrayLayer = region.dstSubresource.baseArrayLayer;
        barriers[1].arrayLayers = region.dstSubresource.layerCount;

        enc->cmd->ImageBarriers(barriers);

        vk::CopyImageInfo2 copyInfo{};
        copyInfo.srcImage = src->texture->image->ApiHandle();
        copyInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        copyInfo.dstImage = dst->texture->image->ApiHandle();
        copyInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        copyInfo.regionCount = 1;
        copyInfo.pRegions = &region;

        enc->cmd->ApiHandle().copyImage2(copyInfo);
        if (src->texture)
            enc->retained.usedTextures.push_back(src->texture);
        if (dst->texture)
            enc->retained.usedTextures.push_back(dst->texture);
    }

    void wgpuCommandEncoderResolveQuerySet(WGPUCommandEncoder enc,
                                           WGPUQuerySet querySet,
                                           uint32_t firstQuery,
                                           uint32_t queryCount,
                                           WGPUBuffer dst,
                                           uint64_t dstOffset)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderResolveQuerySet"))
            return;

        auto deferToSubmit = [&]()
        { enc->deferredResourceError = true; };

        // Validate "invalid"/null/cross-device on both resources first so that
        // those validation errors take precedence over destroyed-deferral.
        if (!querySet || querySet->invalid || querySet->device != enc->device)
        {
            ReportEncoderValidation(enc, "resolveQuerySet: querySet is null/invalid/wrong-device");
            enc->invalid = true;
            return;
        }
        if (!dst || dst->invalid || dst->device != enc->device)
        {
            ReportEncoderValidation(enc, "resolveQuerySet: destination buffer is null/invalid/wrong-device");
            enc->invalid = true;
            return;
        }
        if (querySet->destroyed ||
            dst->internalState == BufferInternalState::Destroyed)
        {
            // Destroyed resources defer to queue.submit() per W3C §3.3.
            deferToSubmit();
            return;
        }
        if (!(dst->usage & WGPUBufferUsage_QueryResolve))
        {
            ReportEncoderValidation(enc, "resolveQuerySet: destination usage must include QUERY_RESOLVE");
            enc->invalid = true;
            return;
        }
        if (static_cast<uint64_t>(firstQuery) + static_cast<uint64_t>(queryCount) > querySet->count)
        {
            ReportEncoderValidation(enc, "resolveQuerySet: firstQuery+queryCount exceeds querySet.count");
            enc->invalid = true;
            return;
        }
        if (dstOffset % 256 != 0)
        {
            ReportEncoderValidation(enc, "resolveQuerySet: destinationOffset must be a multiple of 256");
            enc->invalid = true;
            return;
        }
        if (static_cast<uint64_t>(queryCount) * 8 > dst->size ||
            dstOffset > dst->size - static_cast<uint64_t>(queryCount) * 8)
        {
            ReportEncoderValidation(enc, "resolveQuerySet: resolve range exceeds destination buffer size");
            enc->invalid = true;
            return;
        }

        wgpuQuerySetAddRef(querySet);
        enc->retained.querySets.push_back(querySet);
        enc->retained.usedBuffers.push_back(dst);

        // Destroyed resources defer to queue.submit() per spec.
        if (querySet->destroyed || !querySet->queryPool ||
            dst->internalState == BufferInternalState::Destroyed || !dst->peBuffer)
            return;

        // eWait on an unbegun query stalls forever; zero-fill the range and eWait-copy
        // only begun indices so unused slots resolve to 0 per WebGPU §23.6.
        enc->cmd->ApiHandle().fillBuffer(
            dst->peBuffer->ApiHandle(), dstOffset,
            static_cast<uint64_t>(queryCount) * sizeof(uint64_t), 0u);

        vk::MemoryBarrier2 fillToCopy{};
        fillToCopy.srcStageMask = vk::PipelineStageFlagBits2::eClear;
        fillToCopy.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        fillToCopy.dstStageMask = vk::PipelineStageFlagBits2::eCopy;
        fillToCopy.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
        vk::MemoryBarrier2 workToCopy{};
        workToCopy.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        workToCopy.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
        workToCopy.dstStageMask = vk::PipelineStageFlagBits2::eCopy;
        workToCopy.dstAccessMask = vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite;
        vk::MemoryBarrier2 mbs[2] = {fillToCopy, workToCopy};
        vk::DependencyInfo dep{};
        dep.memoryBarrierCount = 2;
        dep.pMemoryBarriers = mbs;
        enc->cmd->ApiHandle().pipelineBarrier2(dep);

        const uint32_t rangeEnd = firstQuery + queryCount;
        for (uint32_t idx : querySet->beganIndices)
        {
            if (idx < firstQuery || idx >= rangeEnd)
                continue;
            const uint64_t slotOffset = dstOffset +
                                        static_cast<uint64_t>(idx - firstQuery) * sizeof(uint64_t);
            enc->cmd->ApiHandle().copyQueryPoolResults(
                querySet->queryPool, idx, 1,
                dst->peBuffer->ApiHandle(), slotOffset,
                sizeof(uint64_t),
                vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
        }
    }

    void wgpuCommandEncoderWriteTimestamp(WGPUCommandEncoder enc, WGPUQuerySet querySet, uint32_t queryIndex)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderWriteTimestamp"))
            return;
        if (!enc->device || wgpuDeviceHasFeature(enc->device, WGPUFeatureName_TimestampQuery) != WGPU_TRUE)
        {
            ReportEncoderValidation(enc, "writeTimestamp: timestamp-query feature is not enabled");
            enc->invalid = true;
            return;
        }
        if (!querySet || querySet->invalid || querySet->device != enc->device ||
            querySet->type != WGPUQueryType_Timestamp)
        {
            ReportEncoderValidation(enc, "writeTimestamp: querySet is null/invalid/not-timestamp/wrong-device");
            enc->invalid = true;
            return;
        }
        if (querySet->destroyed)
        {
            ReportEncoderValidation(enc, "writeTimestamp: querySet is destroyed");
            enc->invalid = true;
            return;
        }
        if (queryIndex >= querySet->count)
        {
            ReportEncoderValidation(enc, "writeTimestamp: queryIndex exceeds querySet.count");
            enc->invalid = true;
            return;
        }

        wgpuQuerySetAddRef(querySet);
        enc->retained.querySets.push_back(querySet);

        if (querySet->destroyed || !querySet->queryPool)
            return;

        enc->cmd->ApiHandle().writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands,
                                              querySet->queryPool, queryIndex);
    }

    void wgpuCommandEncoderInsertDebugMarker(WGPUCommandEncoder enc, WGPUStringView markerLabel)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderInsertDebugMarker"))
            return;
        enc->cmd->InsertDebugLabel(pwgpu::ToString(markerLabel));
    }

    void wgpuCommandEncoderPushDebugGroup(WGPUCommandEncoder enc, WGPUStringView groupLabel)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderPushDebugGroup"))
            return;
        enc->debugGroupDepth++;
        enc->cmd->BeginDebugRegion(pwgpu::ToString(groupLabel));
    }

    void wgpuCommandEncoderPopDebugGroup(WGPUCommandEncoder enc)
    {
        if (!EncoderOpen(enc, "wgpuCommandEncoderPopDebugGroup"))
            return;
        if (enc->debugGroupDepth == 0)
        {
            ReportEncoderValidation(enc, "popDebugGroup: debug group stack is empty");
            enc->invalid = true;
            return;
        }
        enc->debugGroupDepth--;
        enc->cmd->EndDebugRegion();
    }

    void wgpuCommandEncoderSetLabel(WGPUCommandEncoder enc, WGPUStringView label)
    {
        if (enc)
            enc->label = pwgpu::ToString(label);
    }

    void wgpuCommandBufferAddRef(WGPUCommandBuffer cb)
    {
        if (cb)
            cb->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuCommandBufferRelease(WGPUCommandBuffer cb)
    {
        if (!cb)
            return;
        if (cb->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
        {
            cb->retained.ReleaseAll();
            if (cb->cmd && !cb->submitted)
                cb->cmd->Return();
            delete cb;
        }
    }

    void wgpuCommandBufferSetLabel(WGPUCommandBuffer cb, WGPUStringView label)
    {
        if (cb)
            cb->label = pwgpu::ToString(label);
    }

} // extern "C"
