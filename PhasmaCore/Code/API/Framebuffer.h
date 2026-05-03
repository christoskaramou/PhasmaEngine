#pragma once

namespace pe
{
    class RenderPass;
    class ImageView;

    class Framebuffer : public NoCopy
    {
    public:
        struct Impl;

        static Framebuffer *Create(uint32_t width,
                                   uint32_t height,
                                   const std::vector<ImageView *> &views,
                                   RenderPass *renderPass,
                                   const std::string &name);
        static void Destroy(Framebuffer *&fb);
        static std::vector<Framebuffer *> GetHandles();

        Framebuffer(uint32_t width,
                    uint32_t height,
                    const std::vector<ImageView *> &views,
                    RenderPass *renderPass,
                    const std::string &name);
        ~Framebuffer();

        uvec2 GetSize() const { return m_size; }
        uint32_t GetWidth() const { return m_size.x; }
        uint32_t GetHeight() const { return m_size.y; }
        const std::string &GetName() const { return m_name; }

    private:
        friend struct VulkanFramebufferImpl;
#if defined(PE_WIN32)
        friend struct Dx12FramebufferImpl;
#endif

        Impl *m_impl{};
        uvec2 m_size{};
        std::string m_name;
    };
} // namespace pe
