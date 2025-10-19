#include "API/Framebuffer.h"
#include "API/RenderPass.h"
#include "API/RHI.h"

namespace pe
{
    Framebuffer::Framebuffer(uint32_t width,
                             uint32_t height,
                             const std::vector<vk::ImageView> &views,
                             RenderPass *renderPass,
                             const std::string &name)
        : m_size{width, height}
    {
        vk::FramebufferCreateInfo fbci{};
        fbci.renderPass = renderPass->ApiHandle();
        fbci.attachmentCount = static_cast<uint32_t>(views.size());
        fbci.pAttachments = views.data();
        fbci.width = width;
        fbci.height = height;
        fbci.layers = 1;

        m_apiHandle = RHII.GetDevice().createFramebuffer(fbci);

        Debug::SetObjectName(m_apiHandle, name);
    }

    Framebuffer::~Framebuffer()
    {
        if (m_apiHandle)
            RHII.GetDevice().destroyFramebuffer(m_apiHandle);
    }
}
