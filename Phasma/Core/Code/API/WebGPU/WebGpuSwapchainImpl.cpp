#include "API/WebGPU/WebGpuSwapchainImpl.h"
#include "API/Image.h"
#include "API/ImageView.h"
#include "API/Surface.h"
#include "API/Swapchain.h"
#include "API/WebGPU/WebGpuFormat.h"
#include "API/WebGPU/WebGpuImageImpl.h"
#include "API/WebGPU/WebGpuRhiImpl.h"

namespace pe
{
    namespace
    {
        Rect2Du WindowExtent(SDL_Window *window)
        {
            int width = 0;
            int height = 0;
            if (window)
                SDL_GetWindowSize(window, &width, &height);

            return {0, 0, static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))};
        }
    } // namespace

    WebGpuSurfaceImpl::WebGpuSurfaceImpl(Surface *owner, SDL_Window *window)
        : m_owner{owner},
          m_window{window}
    {
        WebGpuRhiImpl *web = WebGpuRhiImpl::Get();

        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
        canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
        canvas.selector = {"#canvas", WGPU_STRLEN};
        WGPUSurfaceDescriptor surfaceDesc{};
        surfaceDesc.nextInChain = &canvas.chain;
        m_surface = wgpuInstanceCreateSurface(web->m_instance, &surfaceDesc);
        PE_ERROR_IF(!m_surface, "WebGpuSurfaceImpl: no WebGPU surface for #canvas");

        // The first capability format is the browser's preferred canvas format (bgra8unorm on Windows Chrome).
        WGPUSurfaceCapabilities caps{};
        wgpuSurfaceGetCapabilities(m_surface, web->m_adapter, &caps);
        PE_ERROR_IF(caps.formatCount == 0, "WebGpuSurfaceImpl: the canvas reports no formats");
        m_format = caps.formats[0];
        wgpuSurfaceCapabilitiesFreeMembers(caps);

        owner->m_format = FromWgpuSurfaceFormat(m_format);
        PE_ERROR_IF(owner->m_format == PE_FORMAT_UNDEFINED,
                    "WebGpuSurfaceImpl: unexpected canvas format %d",
                    static_cast<int>(m_format));
        owner->m_actualExtent = WindowExtent(window);
    }

    WebGpuSurfaceImpl::~WebGpuSurfaceImpl()
    {
        if (m_surface)
            wgpuSurfaceRelease(m_surface);
    }

    bool WebGpuSurfaceImpl::IsPresentable() const
    {
        const Rect2Du extent = WindowExtent(m_window);
        return extent.width > 0 && extent.height > 0;
    }

    WebGpuSwapchainImpl::WebGpuSwapchainImpl(Swapchain *owner, const SwapchainDesc &desc)
        : m_owner{owner},
          m_surface{desc.surface}
    {
        PE_ERROR_IF(!m_surface, "WebGpuSwapchainImpl: no surface");
        WebGpuSurfaceImpl *surface = WebGpuSurfaceImpl::From(m_surface);

        const Rect2Du window = WindowExtent(surface->m_window);
        const uint32_t width = desc.width ? desc.width : window.width;
        const uint32_t height = desc.height ? desc.height : window.height;
        PE_ERROR_IF(width == 0 || height == 0, "WebGpuSwapchainImpl: invalid canvas extent %ux%u", width, height);

        WGPUSurfaceConfiguration config{};
        config.device = WebGpuRhiImpl::Get()->m_device;
        config.format = surface->m_format;
        config.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst;
        config.width = width;
        config.height = height;
        config.alphaMode = WGPUCompositeAlphaMode_Opaque;
        config.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface->m_surface, &config);

        m_owner->m_width = width;
        m_owner->m_height = height;
        m_owner->m_presentMode = PE_PRESENT_MODE_FIFO;
        m_owner->m_images.resize(kImageCount);
        for (uint32_t i = 0; i < kImageCount; ++i)
        {
            Image *img = new Image();
            img->m_impl = new WebGpuImageImpl(img);
            img->m_width = width;
            img->m_height = height;
            img->m_format = m_surface->GetFormat();
            img->m_usage = PE_IMAGE_USAGE_COLOR_ATTACHMENT | PE_IMAGE_USAGE_TRANSFER_DST;
            img->m_name = "Swapchain_image_" + std::to_string(i);
            img->m_trackInfos.resize(1);

            ImageTrackInfo info{};
            info.image = img;
            info.layout = PE_IMAGE_LAYOUT_PRESENT_SRC;
            info.stageFlags = PE_STAGE_ALL_COMMANDS;
            info.accessMask = PE_ACCESS_NONE;
            img->m_trackInfos[0].resize(1, info);

            ImageViewDesc viewDesc{};
            viewDesc.format = img->GetFormat();
            img->SetRTV(ImageView::Create(img, viewDesc, "Swapchain_image_view" + std::to_string(i)));
            m_owner->m_images[i] = img;
        }
    }

    WebGpuSwapchainImpl::~WebGpuSwapchainImpl()
    {
        wgpuSurfaceUnconfigure(WebGpuSurfaceImpl::From(m_surface)->m_surface);
    }

    bool WebGpuSwapchainImpl::WaitForNextFrame()
    {
        // The canvas never reports Outdated after a resize; it keeps handing out textures of the configured size.
        const Rect2Du window = WindowExtent(WebGpuSurfaceImpl::From(m_surface)->m_window);
        const bool resized = window.width != m_owner->m_width || window.height != m_owner->m_height;
        if (resized && window.width > 0 && window.height > 0)
            throw SwapchainOutOfDateError{};

        return true;
    }

    uint32_t WebGpuSwapchainImpl::AquireNextImage(Semaphore *)
    {
        WGPUSurfaceTexture surfaceTexture{};
        wgpuSurfaceGetCurrentTexture(WebGpuSurfaceImpl::From(m_surface)->m_surface, &surfaceTexture);
        const bool ok = surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal ||
                        surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
        if (!ok)
        {
            if (surfaceTexture.texture)
                wgpuTextureRelease(surfaceTexture.texture);

            throw SwapchainOutOfDateError{};
        }

        const uint32_t index = m_next;
        m_next = (m_next + 1) % kImageCount;
        WebGpuImageImpl::From(m_owner->m_images[index])->SetSwapchainTexture(surfaceTexture.texture);
        return index;
    }
} // namespace pe
