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

#if PE_VULKAN
#include "Renderer/Framebuffer.h"
#include "Renderer/RenderPass.h"
#include "Renderer/RHI.h"

namespace pe
{
	FrameBuffer::FrameBuffer(uint32_t width, uint32_t height, ImageViewHandle view, RenderPass* renderPass)
		: FrameBuffer(width, height, std::vector<ImageViewHandle>{ view }, renderPass)
	{
	}
	
	FrameBuffer::FrameBuffer(uint32_t width, uint32_t height, const std::vector<ImageViewHandle>& views, RenderPass* renderPass)
	{
		this->width = width;
		this->height = height;

		std::vector<VkImageView> _views(views.size());
		for (int i = 0; i < views.size(); i++)
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
		vkCreateFramebuffer(RHII.device, &fbci, nullptr, &frameBuffer);
		m_apiHandle = frameBuffer;
	}

	FrameBuffer::~FrameBuffer()
	{
		if (m_apiHandle)
		{
			vkDestroyFramebuffer(RHII.device, m_apiHandle, nullptr);
			m_apiHandle = {};
		}
	}
}
#endif