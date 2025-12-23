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
} // namespace pe::VulkanHelpers
