#include "vulkanPCH.h"
#include "RenderPass.h"
#include "../VulkanContext/VulkanContext.h"

namespace vm
{
	RenderPass::RenderPass()
	{
		handle = make_ref(vk::RenderPass());
	}

	RenderPass::~RenderPass()
	{
	}

	void RenderPass::Create(const vk::Format& format, const vk::Format& depthFormat)
	{
		Create(std::vector<vk::Format> {format}, depthFormat);
	}

	void RenderPass::Create(const std::vector<vk::Format>& formats, const vk::Format& depthFormat)
	{
		uint32_t size = static_cast<uint32_t>(formats.size());
		bool hasDepth =
				depthFormat == vk::Format::eD32SfloatS8Uint ||
						depthFormat == vk::Format::eD32Sfloat ||
						depthFormat == vk::Format::eD24UnormS8Uint;

		std::vector<vk::AttachmentDescription> attachments(size);
		std::vector<vk::AttachmentReference> colorReferences(size);
		vk::AttachmentReference depthReference;

		for (uint32_t i = 0; i < size; i++)
		{
			attachments[i].format = formats[i];
			attachments[i].samples = vk::SampleCountFlagBits::e1;
			attachments[i].loadOp = vk::AttachmentLoadOp::eClear;
			attachments[i].storeOp = vk::AttachmentStoreOp::eStore;
			attachments[i].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			attachments[i].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			attachments[i].initialLayout = vk::ImageLayout::eUndefined;
			attachments[i].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
			colorReferences[i] = {i, vk::ImageLayout::eColorAttachmentOptimal};
		}

		if (hasDepth)
		{
			vk::AttachmentDescription attachmentDescription;
			attachmentDescription.format = depthFormat;
			attachmentDescription.samples = vk::SampleCountFlagBits::e1;
			attachmentDescription.loadOp = vk::AttachmentLoadOp::eClear;
			attachmentDescription.storeOp = vk::AttachmentStoreOp::eDontCare;
			attachmentDescription.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
			attachmentDescription.stencilStoreOp = vk::AttachmentStoreOp::eStore;
			attachmentDescription.initialLayout = vk::ImageLayout::eUndefined;
			attachmentDescription.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			attachments.push_back(attachmentDescription);
			depthReference = {size, vk::ImageLayout::eDepthStencilAttachmentOptimal};
		}

		std::array<vk::SubpassDescription, 1> subpassDescriptions {};
		subpassDescriptions[0].pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpassDescriptions[0].colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
		subpassDescriptions[0].pColorAttachments = colorReferences.data();
		subpassDescriptions[0].pDepthStencilAttachment = hasDepth ? &depthReference : nullptr;

		vk::RenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
		renderPassInfo.pSubpasses = subpassDescriptions.data();

		handle = make_ref(VulkanContext::get()->device->createRenderPass(renderPassInfo));
	}

	void RenderPass::Destroy()
	{
		if (*handle)
		{
			VulkanContext::get()->device->destroyRenderPass(*handle);
			*handle = nullptr;
		}
	}
}
