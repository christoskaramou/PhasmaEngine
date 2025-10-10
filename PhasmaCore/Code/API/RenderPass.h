#pragma once

namespace pe
{
    class Image;

    class RenderPass : public PeHandle<RenderPass, vk::RenderPass>
    {
    public:
        RenderPass(uint32_t count, Attachment *attachments, const std::string &name);
        ~RenderPass();

    private:
        std::string m_name;
    };
}
