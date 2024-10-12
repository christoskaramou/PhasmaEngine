#pragma once

namespace pe
{
    class Image;

    struct Attachment
    {
        Attachment();

        Image *image;
        AttachmentLoadOp loadOp;
        AttachmentStoreOp storeOp;
        AttachmentLoadOp stencilLoadOp;
        AttachmentStoreOp stencilStoreOp;
        ImageLayout initialLayout; // the layout of the attachment that comes in when the render pass begins
        ImageLayout finalLayout;   // the layout of the attachment that the render pass transitions to when it ends
    };

    class RenderPass : public PeHandle<RenderPass, RenderPassApiHandle>
    {
    public:
        RenderPass(uint32_t count, Attachment *attachments, const std::string &name);
        ~RenderPass();

    private:
        std::string m_name;
    };
}