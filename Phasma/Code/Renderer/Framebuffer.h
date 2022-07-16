#pragma once

namespace pe
{
    class RenderPass;

    class FrameBuffer : public IHandle<FrameBuffer, FrameBufferHandle>
    {
    public:
        FrameBuffer(uint32_t width,
                    uint32_t height,
                    uint32_t count,
                    ImageViewHandle *views,
                    RenderPass *renderPass,
                    const std::string &name);

        ~FrameBuffer();

        uint32_t GetWidth() { return m_width; }

        uint32_t GetHeight() { return m_height; }

    private:
        uint32_t m_width, m_height;
    };
}