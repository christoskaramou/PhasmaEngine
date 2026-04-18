#include "Texture.h"
#include "Device.h"
#include "Utils.h"
#include "FormatMap.h"
#include "API/Queue.h"
#include "API/Semaphore.h"

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
            if (texture->device)
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

        if (texture->invalid)
            return reportValidation("texture is invalid");

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
        }
        if (viewUsage & WGPUTextureUsage_StorageBinding)
        {
            if (!pwgpu::SupportsStorageBinding(resolved.format) &&
                !(tier1 && pwgpu::SupportsStorageBindingTier1(resolved.format)))
                return reportValidation("STORAGE_BINDING requires a storage-capable view format");
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
        if (descriptor && descriptor->label.data)
            view->label = pwgpu::ToString(descriptor->label);

        if (viewUsage & WGPUTextureUsage_RenderAttachment)
        {
            uint32_t mipW = std::max(1u, texture->size.width >> resolved.baseMipLevel);
            uint32_t mipH = std::max(1u, texture->size.height >> resolved.baseMipLevel);
            view->renderExtent = {mipW, mipH, 1};
        }

        if (texture->image)
        {
            VkFormat vkFmt;
            if (resolved.format == texture->format)
                vkFmt = static_cast<VkFormat>(texture->image->GetFormat());
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

            vk::ImageViewCreateInfo ivci = pe::ImageView::CreateInfoInit();
            ivci.image = texture->image->ApiHandle();
            ivci.viewType = vkViewType;
            ivci.format = static_cast<vk::Format>(vkFmt);
            ivci.subresourceRange.aspectMask = pwgpu::ToVkAspect(resolved.aspect, resolved.format);
            ivci.subresourceRange.baseMipLevel = resolved.baseMipLevel;
            ivci.subresourceRange.levelCount = resolved.mipLevelCount;
            ivci.subresourceRange.baseArrayLayer = resolved.baseArrayLayer;
            ivci.subresourceRange.layerCount = resolved.arrayLayerCount;

            const std::string viewName = view->label.empty()
                                             ? (texture->label + "_view")
                                             : view->label;
            try
            {
                view->view = pe::ImageView::Create(texture->image, ivci, viewName);
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
