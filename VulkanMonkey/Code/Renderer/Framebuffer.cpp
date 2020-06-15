#include "Framebuffer.h"
#include "../Renderer/RenderPass.h"
#include "../VulkanContext/VulkanContext.h"

namespace vm
{
	void Framebuffer::Create(uint32_t width, uint32_t height, const vk::ImageView& view, const RenderPass& renderPass)
	{
		Create(width, height, std::vector<vk::ImageView>{ view }, renderPass);
	}

	void Framebuffer::Create(uint32_t width, uint32_t height, const std::vector<vk::ImageView>& views, const RenderPass& renderPass)
	{

		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = *renderPass;
		fbci.attachmentCount = static_cast<uint32_t>(views.size());
		fbci.pAttachments = views.data();
		fbci.width = width;
		fbci.height = height;
		fbci.layers = 1;

		SetRef(VulkanContext::get()->device.createFramebuffer(fbci));
	}
	
	void Framebuffer::Destroy()
	{
		if (!m_ref)
			throw std::runtime_error("m_ref is null");

		if (*m_ref) {
			VulkanContext::get()->device.destroyFramebuffer(*m_ref);
			*m_ref = nullptr;
		}
	}
}
