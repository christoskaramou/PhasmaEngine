/*
Copyright (c) 2018-2021 Christos Karamoustos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "PhasmaPch.h"
#include "RenderPass.h"
#include "RenderApi.h"

namespace pe
{
	RenderPass::RenderPass()
	{
		handle = make_sptr(vk::RenderPass());
	}
	
	RenderPass::~RenderPass()
	{
	}
	
	void RenderPass::Create(const vk::Format& format)
	{
		Create(std::vector<vk::Format> {format});
	}

	bool IsDepthFormat(vk::Format& format)
	{
		return format == vk::Format::eD32SfloatS8Uint ||
			format == vk::Format::eD32Sfloat ||
			format == vk::Format::eD24UnormS8Uint;
	}
	
	void RenderPass::Create(const std::vector<vk::Format>& formats)
	{
		std::vector<vk::AttachmentDescription> attachments{};
		std::vector<vk::AttachmentReference> colorReferences{};
		vk::AttachmentReference depthReference;

		bool hasDepth = false;
		uint32_t size = static_cast<uint32_t>(formats.size());
		uint32_t attachmentIndex = 0;
		for (auto format : formats)
		{
			if (!IsDepthFormat(format))
			{
				vk::AttachmentDescription attachment{};

				attachment.format = format;
				attachment.samples = vk::SampleCountFlagBits::e1;
				attachment.loadOp = vk::AttachmentLoadOp::eClear;
				attachment.storeOp = vk::AttachmentStoreOp::eStore;
				attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
				attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
				attachment.initialLayout = vk::ImageLayout::eUndefined;
				attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
				attachments.push_back(attachment);

				vk::AttachmentReference reference{ attachmentIndex++, vk::ImageLayout::eColorAttachmentOptimal };
				colorReferences.push_back(reference);
			}
			else
			{
				// Only one depth reference sould make it in here, else it will be overwrite
				vk::AttachmentDescription attachmentDescription;
				attachmentDescription.format = format;
				attachmentDescription.samples = vk::SampleCountFlagBits::e1;
				attachmentDescription.loadOp = vk::AttachmentLoadOp::eClear;
				attachmentDescription.storeOp = vk::AttachmentStoreOp::eStore;
				attachmentDescription.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
				attachmentDescription.stencilStoreOp = vk::AttachmentStoreOp::eStore;
				attachmentDescription.initialLayout = vk::ImageLayout::eUndefined;
				attachmentDescription.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
				attachments.push_back(attachmentDescription);
				depthReference = { attachmentIndex++, vk::ImageLayout::eDepthStencilAttachmentOptimal };
				hasDepth = true;
			}
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
		
		handle = make_sptr(VulkanContext::Get()->device->createRenderPass(renderPassInfo));
	}
	
	void RenderPass::Destroy()
	{
		if (*handle)
		{
			VulkanContext::Get()->device->destroyRenderPass(*handle);
			*handle = nullptr;
		}
	}
}
