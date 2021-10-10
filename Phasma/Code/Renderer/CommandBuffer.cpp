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

#include "CommandBuffer.h"
#include "Renderer/Vulkan/Vulkan.h"
#include "Renderer/CommandPool.h"
#include "Renderer/RenderPass.h"
#include "Renderer/FrameBuffer.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Descriptor.h"
#include "Core/Math.h"

namespace pe
{
	void CommandBuffer::Create(CommandPool* commandPool)
	{
		VkCommandBufferAllocateInfo cbai{};
		cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cbai.commandPool = commandPool ? commandPool->Handle() : *VULKAN.commandPool;
		cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cbai.commandBufferCount = 1;
		vkAllocateCommandBuffers(*VULKAN.device, &cbai, &m_handle);
		VULKAN.SetDebugObjectName(vk::CommandBuffer(m_handle), "CommandBuffer");
	}

	void CommandBuffer::Begin()
	{
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(m_handle, &beginInfo);
	}

	void CommandBuffer::End()
	{
		vkEndCommandBuffer(m_handle);
	}

	void CommandBuffer::PipelineBarrier()
	{
	}

	bool DepthFormat(vk::Format& format)
	{
		return format == vk::Format::eD32SfloatS8Uint ||
			format == vk::Format::eD32Sfloat ||
			format == vk::Format::eD24UnormS8Uint;
	}

	void CommandBuffer::BeginPass(RenderPass& pass, FrameBuffer& frameBuffer)
	{
		const vec4 color(0.0f, 0.0f, 0.0f, 1.0f);
		VkClearValue clearColor;
		memcpy(clearColor.color.float32, &color, sizeof(vec4));

		VkClearDepthStencilValue depthStencil;
		depthStencil.depth = 0.f;
		depthStencil.stencil = 0;

		std::vector<VkClearValue> clearValues(pass.formats.size());
		for (int i = 0; i < pass.formats.size(); i++)
		{
			if (DepthFormat(pass.formats[i]))
				clearValues[i].depthStencil = depthStencil;
			else
				clearValues[i].color = clearColor.color;

		}

		VkRenderPassBeginInfo rpi{};
		rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpi.renderPass = *pass.handle;
		rpi.framebuffer = *frameBuffer.handle;
		rpi.renderArea.offset = vk::Offset2D{ 0, 0 };
		rpi.renderArea.extent = vk::Extent2D{ frameBuffer.width, frameBuffer.height };
		rpi.clearValueCount = clearValues.size();
		rpi.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(m_handle, &rpi, VK_SUBPASS_CONTENTS_INLINE);
	}

	void CommandBuffer::EndPass()
	{
		vkCmdEndRenderPass(m_handle);
	}

	void CommandBuffer::BindPipeline(Pipeline& pipeline)
	{
		vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline.handle);
	}

	void CommandBuffer::BindVertexBuffer(Buffer& buffer, size_t offset)
	{
	}

	void CommandBuffer::BindIndexBuffer(Buffer& buffer, size_t offset)
	{
	}

	void CommandBuffer::BindDescriptors(Pipeline& pipeline, uint32_t count, Descriptor* discriptors)
	{
		vkCmdBindDescriptorSets(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline.layout, 0, count, reinterpret_cast<VkDescriptorSet*>(discriptors), 0, nullptr);
	}

	void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
	{
		vkCmdDraw(m_handle, vertexCount, instanceCount, firstVertex, firstInstance);
	}

	void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
	{
	}

	void CommandBuffer::Submit()
	{
	}
}
