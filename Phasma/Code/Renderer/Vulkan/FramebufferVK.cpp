#if PE_VULKAN
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RHI.h"

namespace pe
{
    FrameBuffer::FrameBuffer(uint32_t width,
                             uint32_t height,
                             uint32_t count,
                             ImageViewHandle *views,
                             RenderPass *renderPass,
                             const std::string &name)
    {
        m_size = uvec2(width, height);

        std::vector<VkImageView> _views(count);
        for (uint32_t i = 0; i < count; i++)
            _views[i] = views[i];

        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = renderPass->Handle();
        fbci.attachmentCount = static_cast<uint32_t>(_views.size());
        fbci.pAttachments = _views.data();
        fbci.width = width;
        fbci.height = height;
        fbci.layers = 1;

        VkFramebuffer frameBuffer;
        PE_CHECK(vkCreateFramebuffer(RHII.GetDevice(), &fbci, nullptr, &frameBuffer));
        m_handle = frameBuffer;

        Debug::SetObjectName(m_handle, name);
    }

    FrameBuffer::~FrameBuffer()
    {
        if (m_handle)
        {
            vkDestroyFramebuffer(RHII.GetDevice(), m_handle, nullptr);
            m_handle = {};
        }
    }
}
#endif
