#pragma once

namespace pe
{
    class RenderPass;
    class ImageView;

    class Framebuffer : public PeHandle<Framebuffer, vk::Framebuffer>
    {
    public:
        Framebuffer(uint32_t width,
                    uint32_t height,
                    const std::vector<ImageView *> &views,
                    RenderPass *renderPass,
                    const std::string &name);
        ~Framebuffer();

        uvec2 GetSize() { return m_size; }
        uint32_t GetWidth() { return m_size.x; }
        uint32_t GetHeight() { return m_size.y; }

    private:
        uvec2 m_size;
    };
} // namespace pe
