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

#pragma once

#include "Core/Base.h"

namespace pe
{
	class RenderPass;
	class FrameBuffer;
	class Pipeline;
	class Compute;
	class Buffer;
	class Descriptor;
	class CommandPool;

	enum class BarrierType
	{
		Memory,
		Buffer,
		Image
	};

	class CommandBuffer
	{
	public:
		CommandBuffer();

		~CommandBuffer();

		void Create(CommandPool* commandPool = nullptr); // TODO: Add command pool wrapper

		void Begin();

		void End();

		void PipelineBarrier();

		void BeginPass(RenderPass& pass, FrameBuffer& frameBuffer);

		void EndPass();

		void BindPipeline(Pipeline& pipeline);

		void BindVertexBuffer(Buffer& buffer, size_t offset);

		void BindIndexBuffer(Buffer& buffer, size_t offset);

		void BindDescriptors(Pipeline& pipeline, uint32_t count, Descriptor* discriptors);

		void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);

		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);

		void Submit();

		SPtr<vk::CommandBuffer> handle;
	};
}
