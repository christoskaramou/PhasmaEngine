#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/Image_Internal.h"

namespace pe
{
    class CommandBuffer;

    struct VulkanImageImpl final : public Image::Impl
    {
        VulkanImageImpl(Image *owner, const ImageDesc &desc);
        // Swapchain-image wrapper: owns no allocation, does not destroy.
        VulkanImageImpl(Image *owner, vk::Image externalImage);
        ~VulkanImageImpl() override;

        void CopyImage(CommandBuffer *cmd, Image *src) override;
        void CopyToBuffer(CommandBuffer *cmd, Buffer *dst) override;
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

        // Friend Image in Image.h grants access to Image's private m_impl.
        static VulkanImageImpl *From(Image *image) { return static_cast<VulkanImageImpl *>(image->m_impl); }
        static const VulkanImageImpl *From(const Image *image) { return static_cast<const VulkanImageImpl *>(image->m_impl); }
        static VulkanImageImpl *TryFrom(Image *image) { return image && image->m_impl ? From(image) : nullptr; }
        static const VulkanImageImpl *TryFrom(const Image *image) { return image && image->m_impl ? From(image) : nullptr; }

        Image *m_owner;
        vk::Image m_image;
        VmaAllocation m_allocation{}; // null for swapchain-wrapped images
        vk::Format m_vkFormat{vk::Format::eUndefined};
        PeImageType m_imageType{PE_IMAGE_TYPE_2D};
        bool m_externallyOwned{false};
    };

    // Engine-private Vulkan seam accessors for backend integration.
    // Will move to API/Vulkan/RHI_Vulkan.h when the public Vulkan seam lands.
    inline vk::Image GetVulkanImage(Image *image)
    {
        VulkanImageImpl *impl = VulkanImageImpl::TryFrom(image);
        return impl ? impl->m_image : vk::Image{};
    }

    inline vk::Image GetVulkanImage(const Image *image)
    {
        const VulkanImageImpl *impl = VulkanImageImpl::TryFrom(image);
        return impl ? impl->m_image : vk::Image{};
    }

    inline void SetVulkanImageHandle(Image *image, vk::Image vkImage)
    {
        VulkanImageImpl *impl = VulkanImageImpl::TryFrom(image);
        if (impl)
            impl->m_image = vkImage;
    }

    // Pe<->Vk format translators. Engine-private during Phase 0 (live next to
    // the Vulkan impl). The Pe enum is intentionally a coarse subset of vk
    // formats; unsupported vk values map to PE_FORMAT_UNDEFINED.
    vk::Format ToVkFormat(::PeFormat format);
    ::PeFormat FromVkFormat(vk::Format format);

    vk::SampleCountFlagBits ToVkSampleCount(PeSampleCount samples);
    PeSampleCount FromVkSampleCount(vk::SampleCountFlagBits samples);

    vk::ImageViewType ToVkImageViewType(PeImageViewType type);
    PeImageViewType FromVkImageViewType(vk::ImageViewType type);

    vk::ImageType ToVkImageType(PeImageType type);
    PeImageType FromVkImageType(vk::ImageType type);

    vk::ImageLayout ToVkImageLayout(PeImageLayout layout);
    PeImageLayout FromVkImageLayout(vk::ImageLayout layout);

    vk::ImageUsageFlags ToVkImageUsage(PeImageUsageFlags usage);
    PeImageUsageFlags FromVkImageUsage(vk::ImageUsageFlags usage);

    // Vulkan-private factory used by VulkanSwapchainImpl to wrap externally-owned
    // VkImage handles. Lives here (not Image_Internal.h) so the neutral header
    // does not leak vk::Image.
    Image::Impl *CreateSwapchainImageImpl(Image *owner, vk::Image externalImage);
} // namespace pe
