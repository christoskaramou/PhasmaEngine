#pragma once

namespace pe
{
    class Attachment
    {
    public:
        Attachment();

        AttachmentDescriptionFlags flags;
        Format format;
        SampleCount samples;
        AttachmentLoadOp loadOp;
        AttachmentStoreOp storeOp;
        AttachmentLoadOp stencilLoadOp;
        AttachmentStoreOp stencilStoreOp;
        ImageLayout initialLayout;
        ImageLayout finalLayout;
    };

    class RenderPass : public IHandle<RenderPass, RenderPassHandle>
    {
    public:
        RenderPass(uint32_t count, Attachment *attachments, const std::string &name);

        ~RenderPass();

        std::vector<Attachment> attachments;
    };
}