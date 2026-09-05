#include "Surface.h"
#include "Device.h"
#include "Texture.h"
#include "Adapter.h"
#include "FormatMap.h"
#include "Utils.h"
#include "API/Queue.h"
#include "API/Command.h"
#include "API/Image.h"
#include "API/Swapchain.h"
#include "API/Vulkan/VulkanImageImpl.h"

extern "C" void wgpuDeviceAddRef(WGPUDevice);
extern "C" void wgpuDeviceRelease(WGPUDevice);
extern "C" void wgpuTextureRelease(WGPUTexture);

namespace
{
    WGPUPresentMode PePresentModeToWGPU(PePresentMode mode)
    {
        switch (mode)
        {
        case PE_PRESENT_MODE_IMMEDIATE:
            return WGPUPresentMode_Immediate;
        case PE_PRESENT_MODE_MAILBOX:
            return WGPUPresentMode_Mailbox;
        case PE_PRESENT_MODE_FIFO:
            return WGPUPresentMode_Fifo;
        case PE_PRESENT_MODE_FIFO_RELAXED:
            return WGPUPresentMode_FifoRelaxed;
        default:
            return WGPUPresentMode_Fifo;
        }
    }

    PePresentMode WGPUPresentModeToPe(WGPUPresentMode mode)
    {
        switch (mode)
        {
        case WGPUPresentMode_Immediate:
            return PE_PRESENT_MODE_IMMEDIATE;
        case WGPUPresentMode_Mailbox:
            return PE_PRESENT_MODE_MAILBOX;
        case WGPUPresentMode_FifoRelaxed:
            return PE_PRESENT_MODE_FIFO_RELAXED;
        default:
            return PE_PRESENT_MODE_FIFO;
        }
    }

    // RebuildSwapchain()/RecreateSurface() destroy and recreate the pe objects.
    void RefreshNativeSurface(WGPUSurfaceImpl *surface)
    {
        if (!surface->rhi)
            return;
        surface->surface = surface->rhi->GetSurface();
        surface->swapchain = surface->rhi->GetSwapchain();
    }

    // The RHI creates no swapchain for a WebGPU-owned window and a live one never follows the
    // window size or present mode; mirrors RuntimeSceneRenderer::Resize.
    void RebuildSwapchain(WGPUSurfaceImpl *surface, const WGPUSurfaceConfiguration *config)
    {
        pe::RHI *rhi = surface->rhi;
        pe::Surface *native = rhi->GetSurface();
        if (!native->IsPresentable())
            return;

        rhi->WaitDeviceIdle();
        pe::Swapchain *old = rhi->GetSwapchain();
        pe::Swapchain::Destroy(old);
        delete surface->acquireSemaphore;
        surface->acquireSemaphore = new pe::Semaphore(false, "wgpu_acquire");

        native->SetActualExtent({0, 0, config->width, config->height});
        native->SetPresentMode(WGPUPresentModeToPe(config->presentMode));
        rhi->CreateSwapchain(native);
        RefreshNativeSurface(surface);
    }
} // namespace

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
        {
            if (surface->currentTexture)
                wgpuTextureRelease(surface->currentTexture);
            delete surface->acquireSemaphore;
            if (surface->device)
                wgpuDeviceRelease(surface->device);
            delete surface;
        }
    }

    void wgpuSurfaceConfigure(WGPUSurface surface, WGPUSurfaceConfiguration const *config)
    {
        if (!surface || !config)
            return;

        if (!config->device)
        {
            PE_WARN("[WebGPU] wgpuSurfaceConfigure: device is required");
            return;
        }

        if (config->format == WGPUTextureFormat_Undefined)
        {
            PE_WARN("[WebGPU] wgpuSurfaceConfigure: format must not be Undefined");
            return;
        }

        if (config->usage == WGPUTextureUsage_None)
        {
            PE_WARN("[WebGPU] wgpuSurfaceConfigure: usage must not be None");
            return;
        }

        if (config->width == 0 || config->height == 0)
        {
            PE_WARN("[WebGPU] wgpuSurfaceConfigure: width and height must be > 0");
            return;
        }

        RefreshNativeSurface(surface);
        if (surface->surface)
        {
            WGPUTextureFormat nativeFmt = pwgpu::FromVkFormat(static_cast<VkFormat>(pe::ToVkFormat(surface->surface->GetFormat())));
            if (nativeFmt != WGPUTextureFormat_Undefined && config->format != nativeFmt)
            {
                PE_WARN("[WebGPU] wgpuSurfaceConfigure: requested format does not match native surface format");
                return;
            }
        }

        if (surface->currentTexture)
        {
            wgpuTextureRelease(surface->currentTexture);
            surface->currentTexture = nullptr;
        }
        surface->currentImageIndex = UINT32_MAX;

        if (surface->surface && surface->rhi)
        {
            const WGPUSurfaceConfiguration &prev = surface->configuration;
            if (!surface->swapchain || prev.presentMode != config->presentMode ||
                prev.width != config->width || prev.height != config->height)
                RebuildSwapchain(surface, config);
        }

        if (surface->device != config->device)
        {
            if (surface->device)
                wgpuDeviceRelease(surface->device);
            surface->device = config->device;
            wgpuDeviceAddRef(config->device);
        }

        if (surface->swapchain && !surface->acquireSemaphore)
            surface->acquireSemaphore = new pe::Semaphore(false, "wgpu_acquire");

        surface->configuration = *config;
        surface->configured = true;
    }

    void wgpuSurfaceUnconfigure(WGPUSurface surface)
    {
        if (!surface)
            return;

        if (surface->currentTexture)
        {
            wgpuTextureRelease(surface->currentTexture);
            surface->currentTexture = nullptr;
        }
        surface->currentImageIndex = UINT32_MAX;

        if (surface->device)
        {
            wgpuDeviceRelease(surface->device);
            surface->device = nullptr;
        }

        surface->configured = false;
    }

    WGPUStatus wgpuSurfaceGetCapabilities(WGPUSurface surface, WGPUAdapter adapter,
                                          WGPUSurfaceCapabilities *capabilities)
    {
        if (!capabilities)
            return WGPUStatus_Error;
        if (!surface || !adapter)
            return WGPUStatus_Error;

        std::vector<WGPUPresentMode> presentModes;

        if (surface->surface)
        {
            auto peModes = surface->surface->GetSupportedPresentModes();
            for (auto &m : peModes)
            {
                WGPUPresentMode wm = PePresentModeToWGPU(m);
                bool dup = false;
                for (auto &existing : presentModes)
                {
                    if (existing == wm)
                    {
                        dup = true;
                        break;
                    }
                }
                if (!dup)
                    presentModes.push_back(wm);
            }
        }

        if (presentModes.empty())
            presentModes.push_back(WGPUPresentMode_Fifo);

        std::vector<WGPUTextureFormat> formats;

        if (surface->surface)
        {
            WGPUTextureFormat surfFmt = pwgpu::FromVkFormat(static_cast<VkFormat>(pe::ToVkFormat(surface->surface->GetFormat())));
            if (surfFmt != WGPUTextureFormat_Undefined)
                formats.push_back(surfFmt);
        }

        auto addIfMissing = [&formats](WGPUTextureFormat fmt)
        {
            for (auto &f : formats)
            {
                if (f == fmt)
                    return;
            }
            formats.push_back(fmt);
        };
        addIfMissing(WGPUTextureFormat_BGRA8Unorm);
        addIfMissing(WGPUTextureFormat_RGBA8Unorm);

        auto *fmtArr = new WGPUTextureFormat[formats.size()];
        std::memcpy(fmtArr, formats.data(), formats.size() * sizeof(WGPUTextureFormat));

        auto *pmArr = new WGPUPresentMode[presentModes.size()];
        std::memcpy(pmArr, presentModes.data(), presentModes.size() * sizeof(WGPUPresentMode));

        auto *amArr = new WGPUCompositeAlphaMode[1];
        amArr[0] = WGPUCompositeAlphaMode_Opaque;

        capabilities->formatCount = formats.size();
        capabilities->formats = fmtArr;
        capabilities->presentModeCount = presentModes.size();
        capabilities->presentModes = pmArr;
        capabilities->alphaModeCount = 1;
        capabilities->alphaModes = amArr;

        return WGPUStatus_Success;
    }

    void wgpuSurfaceGetCurrentTexture(WGPUSurface surface, WGPUSurfaceTexture *surfaceTexture)
    {
        if (!surfaceTexture)
            return;

        surfaceTexture->texture = nullptr;

        if (!surface || !surface->configured)
        {
            surfaceTexture->texture = nullptr;
            surfaceTexture->status = WGPUSurfaceGetCurrentTextureStatus_Error;
            return;
        }

        RefreshNativeSurface(surface);
        if (surface->swapchain && !surface->acquireSemaphore)
            surface->acquireSemaphore = new pe::Semaphore(false, "wgpu_acquire");
        if (!surface->swapchain || !surface->acquireSemaphore)
        {
            surfaceTexture->texture = nullptr;
            surfaceTexture->status = WGPUSurfaceGetCurrentTextureStatus_Error;
            return;
        }

        if (surface->currentTexture)
        {
            surface->currentTexture->refCount.fetch_add(1, std::memory_order_relaxed);
            surfaceTexture->texture = surface->currentTexture;
            surfaceTexture->status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
            return;
        }

        uint32_t imageIndex = 0;
        try
        {
            if (!surface->swapchain->WaitForNextFrame())
            {
                surfaceTexture->status = WGPUSurfaceGetCurrentTextureStatus_Timeout;
                return;
            }
            imageIndex = surface->swapchain->AquireNextImage(surface->acquireSemaphore);
        }
        catch (const pe::PresentWaitTimeoutError &)
        {
            surfaceTexture->status = WGPUSurfaceGetCurrentTextureStatus_Timeout;
            return;
        }
        catch (const pe::SwapchainOutOfDateError &)
        {
            surfaceTexture->status = WGPUSurfaceGetCurrentTextureStatus_Outdated;
            return;
        }
        pe::Image *swapImage = surface->swapchain->GetImage(imageIndex);
        if (!swapImage)
        {
            surfaceTexture->texture = nullptr;
            surfaceTexture->status = WGPUSurfaceGetCurrentTextureStatus_Error;
            return;
        }

        if (surface->device && surface->device->peQueue)
        {
            pe::CommandBuffer *syncCmd = surface->device->peQueue->AcquireCommandBuffer();
            syncCmd->Begin();

            pe::ImageBarrierInfo barrier{};
            barrier.image = swapImage;
            barrier.layout = PE_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.stageFlags = PE_STAGE_COLOR_ATTACHMENT_OUTPUT;
            barrier.accessMask = PE_ACCESS_COLOR_ATTACHMENT_WRITE;
            barrier.baseMipLevel = 0;
            barrier.mipLevels = 1;
            barrier.baseArrayLayer = 0;
            barrier.arrayLayers = 1;
            syncCmd->ImageBarrier(barrier);

            syncCmd->End();
            surface->acquireSemaphore->SetStageFlags(PE_STAGE_COLOR_ATTACHMENT_OUTPUT);
            surface->device->peQueue->Submit(1, &syncCmd, surface->acquireSemaphore, nullptr);
            syncCmd->Wait();
            syncCmd->Return();
        }

        pe::Rect2Du extent = surface->surface->GetActualExtent();

        auto *tex = new WGPUTextureImpl();
        tex->isSwapchain = true;
        tex->image = swapImage;
        tex->device = surface->device;
        if (tex->device)
            wgpuDeviceAddRef(tex->device);
        tex->format = surface->configuration.format;
        tex->usage = static_cast<WGPUTextureUsage>(surface->configuration.usage);
        tex->dimension = WGPUTextureDimension_2D;
        tex->size = {extent.width, extent.height, 1};
        tex->mipLevelCount = 1;
        tex->sampleCount = 1;

        surface->currentImageIndex = imageIndex;

        tex->refCount.fetch_add(1, std::memory_order_relaxed);
        surface->currentTexture = tex;

        surfaceTexture->texture = tex;
        surfaceTexture->status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    }

    WGPUStatus wgpuSurfacePresent(WGPUSurface surface)
    {
        if (!surface)
            return WGPUStatus_Error;

        if (!surface->configured)
        {
            PE_WARN("[WebGPU] wgpuSurfacePresent: surface is not configured");
            return WGPUStatus_Error;
        }

        if (!surface->currentTexture || surface->currentImageIndex == UINT32_MAX)
        {
            PE_WARN("[WebGPU] wgpuSurfacePresent: no current texture (call getCurrentTexture first)");
            return WGPUStatus_Error;
        }

        RefreshNativeSurface(surface);
        if (surface->device && surface->device->peQueue && surface->swapchain)
        {
            pe::Image *swapImage =
                surface->swapchain->GetImage(surface->currentImageIndex);
            if (swapImage)
            {
                pe::CommandBuffer *cmd = surface->device->peQueue->AcquireCommandBuffer();
                cmd->Begin();

                pe::ImageBarrierInfo barrier{};
                barrier.image = swapImage;
                barrier.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
                barrier.stageFlags = PE_STAGE_BOTTOM_OF_PIPE;
                barrier.accessMask = PE_ACCESS_NONE;
                barrier.baseMipLevel = 0;
                barrier.mipLevels = 1;
                barrier.baseArrayLayer = 0;
                barrier.arrayLayers = 1;
                cmd->ImageBarrier(barrier);

                cmd->End();
                surface->device->peQueue->Submit(1, &cmd, nullptr, nullptr);
                cmd->Wait();
                cmd->Return();
            }

            try
            {
                surface->device->peQueue->Present(
                    surface->swapchain, surface->currentImageIndex, nullptr);
            }
            catch (const pe::SwapchainOutOfDateError &)
            {
                // The next configure rebuilds the swapchain; the frame is dropped.
            }
        }

        wgpuTextureRelease(surface->currentTexture);
        surface->currentTexture = nullptr;
        surface->currentImageIndex = UINT32_MAX;

        return WGPUStatus_Success;
    }

    void wgpuSurfaceSetLabel(WGPUSurface surface, WGPUStringView label)
    {
        if (surface)
            surface->label = pwgpu::ToString(label);
    }

} // extern "C"
