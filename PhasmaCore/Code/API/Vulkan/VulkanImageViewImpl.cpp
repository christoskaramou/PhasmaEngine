#include "API/Vulkan/VulkanImageViewImpl.h"
#include "API/Debug.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/Vulkan/VulkanImageImpl.h"

namespace pe
{
    vk::ImageAspectFlags ToVkImageAspect(PeImageAspectFlags aspect)
    {
        vk::ImageAspectFlags flags{};
        if (aspect & PE_IMAGE_ASPECT_COLOR)
            flags |= vk::ImageAspectFlagBits::eColor;
        if (aspect & PE_IMAGE_ASPECT_DEPTH)
            flags |= vk::ImageAspectFlagBits::eDepth;
        if (aspect & PE_IMAGE_ASPECT_STENCIL)
            flags |= vk::ImageAspectFlagBits::eStencil;
        return flags;
    }

    PeImageAspectFlags FromVkImageAspect(vk::ImageAspectFlags aspect)
    {
        PeImageAspectFlags flags = PE_IMAGE_ASPECT_NONE;
        if (aspect & vk::ImageAspectFlagBits::eColor)
            flags |= PE_IMAGE_ASPECT_COLOR;
        if (aspect & vk::ImageAspectFlagBits::eDepth)
            flags |= PE_IMAGE_ASPECT_DEPTH;
        if (aspect & vk::ImageAspectFlagBits::eStencil)
            flags |= PE_IMAGE_ASPECT_STENCIL;
        return flags;
    }

    vk::ComponentSwizzle ToVkComponentSwizzle(PeComponentSwizzle swizzle)
    {
        switch (swizzle)
        {
        case PE_COMPONENT_SWIZZLE_IDENTITY:
            return vk::ComponentSwizzle::eIdentity;
        case PE_COMPONENT_SWIZZLE_ZERO:
            return vk::ComponentSwizzle::eZero;
        case PE_COMPONENT_SWIZZLE_ONE:
            return vk::ComponentSwizzle::eOne;
        case PE_COMPONENT_SWIZZLE_R:
            return vk::ComponentSwizzle::eR;
        case PE_COMPONENT_SWIZZLE_G:
            return vk::ComponentSwizzle::eG;
        case PE_COMPONENT_SWIZZLE_B:
            return vk::ComponentSwizzle::eB;
        case PE_COMPONENT_SWIZZLE_A:
            return vk::ComponentSwizzle::eA;
        default:
            return vk::ComponentSwizzle::eIdentity;
        }
    }

    PeComponentSwizzle FromVkComponentSwizzle(vk::ComponentSwizzle swizzle)
    {
        switch (swizzle)
        {
        case vk::ComponentSwizzle::eIdentity:
            return PE_COMPONENT_SWIZZLE_IDENTITY;
        case vk::ComponentSwizzle::eZero:
            return PE_COMPONENT_SWIZZLE_ZERO;
        case vk::ComponentSwizzle::eOne:
            return PE_COMPONENT_SWIZZLE_ONE;
        case vk::ComponentSwizzle::eR:
            return PE_COMPONENT_SWIZZLE_R;
        case vk::ComponentSwizzle::eG:
            return PE_COMPONENT_SWIZZLE_G;
        case vk::ComponentSwizzle::eB:
            return PE_COMPONENT_SWIZZLE_B;
        case vk::ComponentSwizzle::eA:
            return PE_COMPONENT_SWIZZLE_A;
        default:
            return PE_COMPONENT_SWIZZLE_IDENTITY;
        }
    }

    VulkanImageViewImpl::VulkanImageViewImpl(ImageView *owner, const ImageViewDesc &desc)
        : m_owner{owner}
    {
        PE_ERROR_IF(!owner->m_parent, "ImageView: parent image is null");

        const ::PeFormat format = desc.format == PE_FORMAT_UNDEFINED ? owner->m_parent->GetFormat() : desc.format;

        vk::ImageViewCreateInfo info{};
        info.image = GetVulkanImage(owner->m_parent);
        info.viewType = ToVkImageViewType(desc.viewType);
        info.format = ToVkFormat(format);
        info.components.r = ToVkComponentSwizzle(desc.swizzleR);
        info.components.g = ToVkComponentSwizzle(desc.swizzleG);
        info.components.b = ToVkComponentSwizzle(desc.swizzleB);
        info.components.a = ToVkComponentSwizzle(desc.swizzleA);
        info.subresourceRange.aspectMask = ToVkImageAspect(desc.aspectMask);
        info.subresourceRange.baseMipLevel = desc.baseMipLevel;
        info.subresourceRange.levelCount = desc.levelCount;
        info.subresourceRange.baseArrayLayer = desc.baseArrayLayer;
        info.subresourceRange.layerCount = desc.layerCount;

        m_imageView = VulkanRhi::Device().createImageView(info);
        Debug::SetObjectName(m_imageView, owner->m_name);
    }

    VulkanImageViewImpl::VulkanImageViewImpl(ImageView *owner, const vk::ImageViewCreateInfo &info)
        : m_owner{owner}
    {
        m_imageView = VulkanRhi::Device().createImageView(info);
        Debug::SetObjectName(m_imageView, owner->m_name);
    }

    VulkanImageViewImpl::~VulkanImageViewImpl()
    {
        if (m_imageView)
            VulkanRhi::Device().destroyImageView(m_imageView);
    }

    ImageView *VulkanImageViewImpl::Create(Image *parent, const vk::ImageViewCreateInfo &info, const std::string &name)
    {
        ImageViewDesc desc{};
        desc.viewType = FromVkImageViewType(info.viewType);
        desc.format = FromVkFormat(info.format);
        desc.aspectMask = FromVkImageAspect(info.subresourceRange.aspectMask);
        desc.baseMipLevel = info.subresourceRange.baseMipLevel;
        desc.levelCount = info.subresourceRange.levelCount;
        desc.baseArrayLayer = info.subresourceRange.baseArrayLayer;
        desc.layerCount = info.subresourceRange.layerCount;
        desc.swizzleR = FromVkComponentSwizzle(info.components.r);
        desc.swizzleG = FromVkComponentSwizzle(info.components.g);
        desc.swizzleB = FromVkComponentSwizzle(info.components.b);
        desc.swizzleA = FromVkComponentSwizzle(info.components.a);

        ImageView *view = new ImageView(parent, desc, name, true);
        view->m_impl = new VulkanImageViewImpl(view, info);
#if defined(PE_TRACK_RESOURCES)
        PeTracker::Track(typeid(ImageView), reinterpret_cast<void *>(view));
#if !defined(PE_TRACK_RESOURCES_NOSPAM)
        PE_INFO("Object ImageView created (Handle: %p)", reinterpret_cast<void *>(view));
#endif
#endif
        return view;
    }

    ImageView::Impl *CreateImageViewImpl(ImageView *owner, const ImageViewDesc &desc)
    {
        return new VulkanImageViewImpl(owner, desc);
    }
} // namespace pe
