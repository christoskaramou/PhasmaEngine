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

        uvec2 GetSize() { return m_size; }

        uint32_t GetWidth() { return m_size.x; }

        uint32_t GetHeight() { return m_size.y; }

    private:
        uvec2 m_size;
    };
}