#pragma once

#include "API/RHITypes.h"

namespace pe
{
    class Image;

    struct ImageViewDesc
    {
        PeImageViewType viewType = PE_IMAGE_VIEW_TYPE_2D;
        ::PeFormat format = PE_FORMAT_UNDEFINED;
        PeImageAspectFlags aspectMask = PE_IMAGE_ASPECT_COLOR;
        uint32_t baseMipLevel = 0;
        uint32_t levelCount = 1;
        uint32_t baseArrayLayer = 0;
        uint32_t layerCount = 1;
        PeComponentSwizzle swizzleR = PE_COMPONENT_SWIZZLE_IDENTITY;
        PeComponentSwizzle swizzleG = PE_COMPONENT_SWIZZLE_IDENTITY;
        PeComponentSwizzle swizzleB = PE_COMPONENT_SWIZZLE_IDENTITY;
        PeComponentSwizzle swizzleA = PE_COMPONENT_SWIZZLE_IDENTITY;
    };

    class ImageView : public NoCopy
    {
    public:
        struct Impl;

        static ImageView *Create(Image *parent, const ImageViewDesc &desc, const std::string &name = "");
        static void Destroy(ImageView *&view);
        static std::vector<ImageView *> GetHandles();

        Image *GetParent() { return m_parent; }
        const ImageViewDesc &GetDesc() const { return m_desc; }
        const std::string &GetName() const { return m_name; }

    private:
        friend struct VulkanImageViewImpl;
        friend struct Dx12ImageViewImpl;

        ImageView(Image *parent, const ImageViewDesc &desc, const std::string &name);
        ImageView(Image *parent, const ImageViewDesc &desc, const std::string &name, bool deferImpl);
        ~ImageView();

        Impl *m_impl{};
        Image *m_parent{};
        ImageViewDesc m_desc{};
        std::string m_name{};
    };
} // namespace pe
