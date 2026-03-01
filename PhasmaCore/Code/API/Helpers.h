#pragma once

namespace pe::VulkanHelpers
{
    inline static bool IsDepthAndStencil(vk::Format format)
    {
        switch (format)
        {
        case vk::Format::eD24UnormS8Uint:
        case vk::Format::eD32SfloatS8Uint:
            return true;
        default:
            return false;
        }
    }

    inline static bool IsDepthOnly(vk::Format format)
    {
        switch (format)
        {
        case vk::Format::eD32Sfloat:
            return true;
        default:
            return false;
        }
    }

    inline static bool IsStencilOnly(vk::Format format)
    {
        switch (format)
        {
        case vk::Format::eS8Uint:
            return true;
        default:
            return false;
        }
    }

    inline static bool HasDepth(vk::Format format)
    {
        return IsDepthOnly(format) || IsDepthAndStencil(format);
    }

    inline static bool HasStencil(vk::Format format)
    {
        return IsStencilOnly(format) || IsDepthAndStencil(format);
    }

    inline static bool HasDepthOrStencil(vk::Format format)
    {
        return IsDepthOnly(format) || IsStencilOnly(format) || IsDepthAndStencil(format);
    }

    inline static vk::ImageAspectFlags GetAspectMask(vk::Format format)
    {
        vk::ImageAspectFlags flags{};

        if (HasDepth(format))
            flags |= vk::ImageAspectFlagBits::eDepth;

        if (HasStencil(format))
            flags |= vk::ImageAspectFlagBits::eStencil;

        if (!flags)
            flags = vk::ImageAspectFlagBits::eColor;

        return flags;
    }

    inline static bool IsReadOnlyAccess(vk::AccessFlags2 accessMask)
    {
        constexpr vk::AccessFlags2 kWriteAccessMask =
            vk::AccessFlagBits2::eShaderWrite |
            vk::AccessFlagBits2::eColorAttachmentWrite |
            vk::AccessFlagBits2::eDepthStencilAttachmentWrite |
            vk::AccessFlagBits2::eTransferWrite |
            vk::AccessFlagBits2::eHostWrite |
            vk::AccessFlagBits2::eMemoryWrite;

        if (accessMask == vk::AccessFlags2{})
            return false;

        return (accessMask & kWriteAccessMask) == vk::AccessFlags2{};
    }
} // namespace pe::VulkanHelpers
