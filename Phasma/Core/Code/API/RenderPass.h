#pragma once

namespace pe
{
    class Image;
    struct Attachment;

    class RenderPass : public NoCopy
    {
    public:
        struct Impl;

        static RenderPass *Create(uint32_t count, Attachment *attachments, const std::string &name);
        static void Destroy(RenderPass *&rp);
        static std::vector<RenderPass *> GetHandles();

        RenderPass(uint32_t count, Attachment *attachments, const std::string &name);
        ~RenderPass();

        const std::string &GetName() const { return m_name; }

    private:
        friend struct VulkanRenderPassImpl;
#if defined(PE_WIN32)
        friend struct Dx12RenderPassImpl;
#endif

        Impl *m_impl{};
        std::string m_name;
    };
} // namespace pe
