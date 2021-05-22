#include "PhasmaPch.h"
#include "Framebuffer.h"
#include "../Renderer/RenderPass.h"
#include "../Renderer/Vulkan/Vulkan.h"

namespace pe
{
	Framebuffer::Framebuffer()
	{
		handle = make_ref(vk::Framebuffer());
	}

	void Framebuffer::Create(uint32_t width, uint32_t height, const vk::ImageView& view, const RenderPass& renderPass)
	{
		Create(width, height, std::vector<vk::ImageView> {view}, renderPass);
	}

	void Framebuffer::Create(
			uint32_t width, uint32_t height, const std::vector<vk::ImageView>& views, const RenderPass& renderPass
	)
	{

		vk::FramebufferCreateInfo fbci;
		fbci.renderPass = *renderPass.handle;
		fbci.attachmentCount = static_cast<uint32_t>(views.size());
		fbci.pAttachments = views.data();
		fbci.width = width;
		fbci.height = height;
		fbci.layers = 1;

		handle = make_ref(VulkanContext::Get()->device->createFramebuffer(fbci));
	}

	void Framebuffer::Destroy()
	{
		if (*handle)
		{
			VulkanContext::Get()->device->destroyFramebuffer(*handle);
			*handle = nullptr;
		}
	}
}
