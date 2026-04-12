#include "Surface.h"
#include "Texture.h"
#include "Adapter.h"
#include "FormatMap.h"
#include "Utils.h"

extern "C"
{

    void wgpuSurfaceAddRef(WGPUSurface surface)
    {
        if (surface)
            surface->refCount.fetch_add(1, std::memory_order_relaxed);
    }

    void wgpuSurfaceRelease(WGPUSurface surface)
    {
        if (!surface)
            return;
        if (surface->refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete surface;
    }

    void wgpuSurfaceConfigure(WGPUSurface surface, WGPUSurfaceConfiguration const *config)
    {
        if (!surface || !config)
            return;
        surface->configuration = *config;
        surface->configured = true;
    }

    void wgpuSurfaceUnconfigure(WGPUSurface surface)
    {
        if (surface)
            surface->configured = false;
    }

    WGPUStatus wgpuSurfaceGetCapabilities(WGPUSurface surface, WGPUAdapter adapter,
                                          WGPUSurfaceCapabilities *capabilities)
    {
        (void)surface;
        (void)adapter;
        if (!capabilities)
            return WGPUStatus_Error;

        static const WGPUTextureFormat s_formats[] = {WGPUTextureFormat_BGRA8Unorm, WGPUTextureFormat_RGBA8Unorm};
        static const WGPUPresentMode s_presentModes[] = {WGPUPresentMode_Fifo, WGPUPresentMode_Immediate, WGPUPresentMode_Mailbox};
        static const WGPUCompositeAlphaMode s_alphaModes[] = {WGPUCompositeAlphaMode_Opaque};

        capabilities->formatCount = 2;
        capabilities->formats = s_formats;
        capabilities->presentModeCount = 3;
        capabilities->presentModes = s_presentModes;
        capabilities->alphaModeCount = 1;
        capabilities->alphaModes = s_alphaModes;

        return WGPUStatus_Success;
    }

    void wgpuSurfaceGetCurrentTexture(WGPUSurface surface, WGPUSurfaceTexture *surfaceTexture)
    {
        if (!surfaceTexture)
            return;

        if (!surface || !surface->swapchain)
        {
            surfaceTexture->texture = nullptr;
            surfaceTexture->status = WGPUSurfaceGetCurrentTextureStatus_Error;
            return;
        }

        auto *tex = new WGPUTextureImpl();
        tex->isSwapchain = true;
        tex->format = surface->configured ? surface->configuration.format : WGPUTextureFormat_BGRA8Unorm;
        tex->usage = WGPUTextureUsage_RenderAttachment;
        tex->dimension = WGPUTextureDimension_2D;
        tex->size = {pe::RHI::Get()->GetWidth(), pe::RHI::Get()->GetHeight(), 1};
        tex->mipLevelCount = 1;
        tex->sampleCount = 1;

        surfaceTexture->texture = tex;
        surfaceTexture->status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    }

    WGPUStatus wgpuSurfacePresent(WGPUSurface surface)
    {
        (void)surface;
        return WGPUStatus_Success;
    }

    void wgpuSurfaceSetLabel(WGPUSurface surface, WGPUStringView label)
    {
        if (surface)
            surface->label = pwgpu::ToString(label);
    }

} // extern "C"
