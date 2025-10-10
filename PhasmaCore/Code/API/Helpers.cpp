namespace pe
{
    bool IsDepthAndStencil(vk::Format format)
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

    bool IsDepthOnly(vk::Format format)
    {
        switch (format)
        {
        case vk::Format::eD32Sfloat:
            return true;
        default:
            return false;
        }
    }

    bool IsStencilOnly(vk::Format format)
    {
        switch (format)
        {
        case vk::Format::eS8Uint:
            return true;
        default:
            return false;
        }
    }

    bool HasDepth(vk::Format format)
    {
        return IsDepthOnly(format) || IsDepthAndStencil(format);
    }

    bool HasStencil(vk::Format format)
    {
        return IsStencilOnly(format) || IsDepthAndStencil(format);
    }

    vk::ImageAspectFlags GetAspectMask(vk::Format format)
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
}
