#ifdef PE_VULKAN
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RHI.h"

namespace pe
{
    Framebuffer::Framebuffer(uint32_t width,
                             uint32_t height,
                             uint32_t count,
                             ImageViewApiHandle *views,
                             RenderPass *renderPass,
                             const std::string &name)
    {
        m_size = uvec2(width, height);

        std::vector<VkImageView> _views(count);
        for (uint32_t i = 0; i < count; i++)
            _views[i] = views[i];

        VkFramebufferCreateInfo fbci{};
        fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass = renderPass->ApiHandle();
        fbci.attachmentCount = static_cast<uint32_t>(_views.size());
        fbci.pAttachments = _views.data();
        fbci.width = width;
        fbci.height = height;
        fbci.layers = 1;

        VkFramebuffer framebuffer;
        PE_CHECK(vkCreateFramebuffer(RHII.GetDevice(), &fbci, nullptr, &framebuffer));
        m_apiHandle = framebuffer;

        Debug::SetObjectName(m_apiHandle, name);
    }

    Framebuffer::~Framebuffer()
    {
        if (m_apiHandle)
        {
            vkDestroyFramebuffer(RHII.GetDevice(), m_apiHandle, nullptr);
            m_apiHandle = {};
        }
    }
}
#endif
