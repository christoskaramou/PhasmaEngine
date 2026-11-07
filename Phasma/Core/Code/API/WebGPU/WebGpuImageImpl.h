#pragma once

#include "API/ImageView_Internal.h"
#include "API/Image_Internal.h"

#include <webgpu/webgpu.h>

namespace pe
{
    // A WGPUTexture behind an engine Image. Swapchain images do not own theirs: the swapchain hands each one the
    // canvas texture at acquire time (WebGpuSwapchainImpl).
    struct WebGpuImageImpl final : public Image::Impl
    {
        explicit WebGpuImageImpl(Image *owner);               // swapchain image, texture set per acquire
        WebGpuImageImpl(Image *owner, const ImageDesc &desc); // owned texture
        ~WebGpuImageImpl() override;

        static WebGpuImageImpl *From(Image *image) { return static_cast<WebGpuImageImpl *>(image->m_impl); }

        void CopyImage(CommandBuffer *cmd, Image *src) override;
        void CopyToBuffer(CommandBuffer *cmd, Buffer *dst, uint32_t mipLevel, uint32_t baseArrayLayer, uint32_t layerCount) override;
        void Blit(CommandBuffer *cmd, Image *src, const ImageBlit &region, PeFilter filter) override;
        void CopyDataToImageStaged(CommandBuffer *cmd,
                                   void *data,
                                   size_t size,
                                   uint32_t baseArrayLayer,
                                   uint32_t layerCount,
                                   uint32_t mipLevel) override;
        void CreateRTV() override;
        void CreateSRV(PeImageViewType type, int mip) override;
        void CreateUAV(PeImageViewType type, uint32_t mip) override;

        // A linear blit chain, mip by mip and layer by layer; replaces SPD on the web.
        void GenerateMipMaps(CommandBuffer *cmd);

        // Swapchain images: take the canvas texture for this frame and rebuild the render-target view on it.
        void SetSwapchainTexture(WGPUTexture texture);

        Image *m_owner = nullptr;
        WGPUTexture m_texture = nullptr;
        WGPUTextureFormat m_format = WGPUTextureFormat_Undefined;
        bool m_ownsTexture = false;
    };

    struct WebGpuImageViewImpl final : public ImageView::Impl
    {
        WebGpuImageViewImpl(ImageView *owner, const ImageViewDesc &desc);
        ~WebGpuImageViewImpl() override;

        static WebGpuImageViewImpl *From(ImageView *view) { return static_cast<WebGpuImageViewImpl *>(view->m_impl); }

        // (Re)creates the view on the parent's current texture; null while the parent has none.
        void Rebuild();

        ImageView *m_owner = nullptr;
        WGPUTextureView m_view = nullptr;
    };
} // namespace pe
