#pragma once

#include "API/Framebuffer.h"

namespace pe
{
    class RenderPass;
    class ImageView;

    struct Framebuffer::Impl : public NoCopy
    {
        virtual ~Impl() = default;
    };

    Framebuffer::Impl *CreateFramebufferImpl(Framebuffer *owner,
                                             uint32_t width,
                                             uint32_t height,
                                             const std::vector<ImageView *> &views,
                                             RenderPass *renderPass,
                                             const std::string &name);
} // namespace pe
