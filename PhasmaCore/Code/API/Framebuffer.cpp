#include "API/Framebuffer.h"
#include "API/Image.h"
#include "API/RHI.h"
#include "API/Vulkan/RHI_Vulkan.h"
#include "API/RenderPass.h"
#include "API/Vulkan/VulkanImageViewImpl.h"

namespace pe
{
    Framebuffer::Framebuffer(uint32_t width,
                             uint32_t height,
                             const std::vector<ImageView *> &views,
                             RenderPass *renderPass,
                             const std::string &name)
        : m_size{width, height}
    {
        std::vector<vk::ImageView> vkViews;
        vkViews.reserve(views.size());
        for (auto view : views)
            vkViews.push_back(pe::GetVulkanImageView(view));

        vk::FramebufferCreateInfo fbci{};
        fbci.renderPass = renderPass->ApiHandle();
        fbci.attachmentCount = static_cast<uint32_t>(views.size());
        fbci.pAttachments = vkViews.data();
        fbci.width = width;
        fbci.height = height;
        fbci.layers = 1;

        m_apiHandle = VulkanRhi::Device().createFramebuffer(fbci);

        Debug::SetObjectName(m_apiHandle, name);
    }

    Framebuffer::~Framebuffer()
    {
        if (m_apiHandle)
            VulkanRhi::Device().destroyFramebuffer(m_apiHandle);
    }
} // namespace pe
