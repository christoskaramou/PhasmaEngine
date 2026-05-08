#pragma once

#include "API/Vulkan/VulkanHeaders.h"

#include "API/ImageView_Internal.h"

namespace pe
{
    struct VulkanImageViewImpl final : public ImageView::Impl
    {
        VulkanImageViewImpl(ImageView *owner, const ImageViewDesc &desc);
        VulkanImageViewImpl(ImageView *owner, const vk::ImageViewCreateInfo &info);
        ~VulkanImageViewImpl() override;

        static ImageView *Create(Image *parent, const vk::ImageViewCreateInfo &info, const std::string &name = "");

        static VulkanImageViewImpl *From(ImageView *view) { return static_cast<VulkanImageViewImpl *>(view->m_impl); }
        static const VulkanImageViewImpl *From(const ImageView *view) { return static_cast<const VulkanImageViewImpl *>(view->m_impl); }
        static VulkanImageViewImpl *TryFrom(ImageView *view) { return view && view->m_impl ? From(view) : nullptr; }
        static const VulkanImageViewImpl *TryFrom(const ImageView *view) { return view && view->m_impl ? From(view) : nullptr; }

        ImageView *m_owner{};
        vk::ImageView m_imageView{};
    };

    inline vk::ImageView GetVulkanImageView(ImageView *view)
    {
        VulkanImageViewImpl *impl = VulkanImageViewImpl::TryFrom(view);
        return impl ? impl->m_imageView : vk::ImageView{};
    }

    inline vk::ImageView GetVulkanImageView(const ImageView *view)
    {
        const VulkanImageViewImpl *impl = VulkanImageViewImpl::TryFrom(view);
        return impl ? impl->m_imageView : vk::ImageView{};
    }

    vk::ImageAspectFlags ToVkImageAspect(PeImageAspectFlags aspect);
    PeImageAspectFlags FromVkImageAspect(vk::ImageAspectFlags aspect);

    vk::ComponentSwizzle ToVkComponentSwizzle(PeComponentSwizzle swizzle);
    PeComponentSwizzle FromVkComponentSwizzle(vk::ComponentSwizzle swizzle);
} // namespace pe
