#pragma once

#include "API/ImageView.h"

namespace pe
{
    struct ImageView::Impl : public NoCopy
    {
        virtual ~Impl() = default;
    };

    ImageView::Impl *CreateImageViewImpl(ImageView *owner, const ImageViewDesc &desc);
} // namespace pe
