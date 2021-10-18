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
#include "Framebuffer.h"
#include "RenderPass.h"
#include "Renderer/Vulkan/Vulkan.h"

namespace pe
{
	FrameBuffer::FrameBuffer()
	{
		handle = {};
	}
	
	void FrameBuffer::Create(uint32_t width, uint32_t height, ImageViewHandle view, RenderPass renderPass)
	{
		Create(width, height, std::vector<ImageViewHandle> {view}, renderPass);
	}
	
	void FrameBuffer::Create(uint32_t width, uint32_t height, std::vector<ImageViewHandle>& views, RenderPass renderPass)
	{
		this->width = width;
		this->height = height;

		std::vector<VkImageView> _views(views.size());
		for (int i = 0; i < views.size(); i++)
			_views[i] = views[i];

		VkFramebufferCreateInfo fbci{};
		fbci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbci.renderPass = renderPass.handle;
		fbci.attachmentCount = static_cast<uint32_t>(_views.size());
		fbci.pAttachments = _views.data();
		fbci.width = width;
		fbci.height = height;
		fbci.layers = 1;
		
		VkFramebuffer frameBuffer;
		vkCreateFramebuffer(*VULKAN.device, &fbci, nullptr, &frameBuffer);
		handle = frameBuffer;
	}
	
	void FrameBuffer::Destroy()
	{
		if (VkFramebuffer(handle))
		{
			vkDestroyFramebuffer(*VULKAN.device, handle, nullptr);
			handle = {};
		}
	}
}
