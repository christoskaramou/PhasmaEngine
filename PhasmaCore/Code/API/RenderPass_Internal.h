#pragma once

#include "API/RenderPass.h"

namespace pe
{
    struct Attachment;

    struct RenderPass::Impl : public NoCopy
    {
        virtual ~Impl() = default;
    };

    RenderPass::Impl *CreateRenderPassImpl(RenderPass *owner, uint32_t count, Attachment *attachments, const std::string &name);
} // namespace pe
