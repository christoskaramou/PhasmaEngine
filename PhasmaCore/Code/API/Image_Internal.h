#pragma once

#include "API/Image.h"

namespace pe
{
    class CommandBuffer;

    struct Image::Impl : public NoCopy
    {
        virtual ~Impl() = default;
        virtual void CopyImage(CommandBuffer *cmd, Image *src) = 0;
        virtual void CopyToBuffer(CommandBuffer *cmd, Buffer *dst) = 0;
        virtual void Blit(CommandBuffer *cmd, Image *src, const ImageBlit &region, PeFilter filter) = 0;
        virtual void CopyDataToImageStaged(CommandBuffer *cmd,
                                           void *data,
                                           size_t size,
                                           uint32_t baseArrayLayer,
                                           uint32_t layerCount,
                                           uint32_t mipLevel) = 0;
        virtual void CreateRTV() = 0;
        virtual void CreateSRV(PeImageViewType type, int mip) = 0;
        virtual void CreateUAV(PeImageViewType type, uint32_t mip) = 0;
    };

    // Phase 0 backend factory + free-function dispatch. In Phase 0 these route
    // unconditionally to the Vulkan implementation; Phase 1 will dispatch on the
    // active RHI backend selected at RHI::Init(). Defined in the active backend's
    // translation unit (currently Vulkan/VulkanImageImpl.cpp).
    Image::Impl *CreateImageImpl(Image *owner, const ImageDesc &desc);
    // CreateSwapchainImageImpl(vk::Image) is Vulkan-private — declared in Vulkan/VulkanImageImpl.h.
    void Image_Barrier_Backend(CommandBuffer *cmd, const ImageBarrierInfo &info);
    void Image_Barriers_Backend(CommandBuffer *cmd, const std::vector<ImageBarrierInfo> &infos);
} // namespace pe
