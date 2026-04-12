#include "Texture.h"
#include "Utils.h"
#include "FormatMap.h"

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
                pe::Image::Destroy(texture->image);
            delete texture;
        }
    }

    void wgpuTextureDestroy(WGPUTexture texture)
    {
        if (!texture || texture->destroyed)
            return;
        texture->destroyed = true;
        if (texture->image && !texture->isSwapchain)
        {
            pe::Image::Destroy(texture->image);
            texture->image = nullptr;
        }
    }

    WGPUTextureView wgpuTextureCreateView(WGPUTexture texture, WGPUTextureViewDescriptor const *descriptor)
    {
        if (!texture)
            return nullptr;

        // Null descriptor and zero-init descriptor must resolve identically (spec §6);
        // seed sentinels so the resolution algorithm below runs once, uniformly.
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

        if (resolved.format == WGPUTextureFormat_Undefined)
            resolved.format = texture->format;

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
            // Only 2D textures interpret depthOrArrayLayers as layers; 1D/3D are always 1.
            const uint32_t textureArrayLayers = (texture->dimension == WGPUTextureDimension_2D)
                                                    ? texture->size.depthOrArrayLayers
                                                    : 1u;
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

        if (resolved.aspect == WGPUTextureAspect_Undefined)
            resolved.aspect = WGPUTextureAspect_All;

        if (resolved.usage == WGPUTextureUsage_None)
            resolved.usage = texture->usage;

        auto *view = new WGPUTextureViewImpl();
        wgpuTextureAddRef(texture);
        view->texture = texture;
        view->format = resolved.format;
        view->dimension = resolved.dimension;
        view->baseMipLevel = resolved.baseMipLevel;
        view->mipLevelCount = resolved.mipLevelCount;
        view->baseArrayLayer = resolved.baseArrayLayer;
        view->arrayLayerCount = resolved.arrayLayerCount;
        view->aspect = resolved.aspect;
        if (descriptor && descriptor->label.data)
            view->label = pwgpu::ToString(descriptor->label);
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
