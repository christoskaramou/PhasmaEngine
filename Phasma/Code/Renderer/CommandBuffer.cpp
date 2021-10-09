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

namespace pe
{
	CommandBuffer::CommandBuffer()
	{
		handle = make_sptr(vk::CommandBuffer());
	}

	CommandBuffer::~CommandBuffer()
	{
	}

	void CommandBuffer::Create(CommandPool* commandPool)
	{
		vk::CommandBufferAllocateInfo cbai;
		cbai.commandPool = commandPool ? commandPool->Handle() : *VULKAN.commandPool;
		cbai.level = vk::CommandBufferLevel::ePrimary;
		cbai.commandBufferCount = 1;
		handle = make_sptr(VULKAN.device->allocateCommandBuffers(cbai).at(0));
		VULKAN.SetDebugObjectName(*handle, "CommandBuffer");
	}

	void CommandBuffer::Begin()
	{
	}

	void CommandBuffer::End()
	{
	}

	void CommandBuffer::PipelineBarrier()
	{
	}

	void CommandBuffer::BeginPass(RenderPass& pass, FrameBuffer& frameBuffer)
	{
	}

	void CommandBuffer::EndPass()
	{
	}

	void CommandBuffer::BindPipeline(Pipeline& pipeline)
	{
	}

	void CommandBuffer::BindVertexBuffer(Buffer& buffer, size_t offset)
	{
	}

	void CommandBuffer::BindIndexBuffer(Buffer& buffer, size_t offset)
	{
	}

	void CommandBuffer::BindDescriptors(Pipeline& pipeline, uint32_t count, Descriptor* discriptors)
	{
	}

	void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
	{
	}

	void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
	{
	}

	void CommandBuffer::Submit()
	{
	}
}
