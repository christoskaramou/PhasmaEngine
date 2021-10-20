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
#include "Renderer/Descriptor.h"

namespace pe
{
	class RenderPass;
	class FrameBuffer;
	class Pipeline;
	class Compute;
	class Buffer;
	class CommandPool;

	enum class BarrierType
	{
		Memory,
		Buffer,
		Image
	};

	struct BufferCopy
	{
		uint64_t srcOffset;
		uint64_t dstOffset;
		uint64_t size;
	};

	struct ImageSubresourceLayers
	{
		ImageAspectFlags aspectMask;
		uint32_t mipLevel;
		uint32_t baseArrayLayer;
		uint32_t layerCount;
	};

	struct Offset3D
	{
		int32_t x;
		int32_t y;
		int32_t z;
	};

	struct ImageBlit
	{
		ImageSubresourceLayers srcSubresource;
		Offset3D srcOffsets[2];
		ImageSubresourceLayers dstSubresource;
		Offset3D dstOffsets[2];
	};

	class CommandBuffer
	{
	public:
		CommandBuffer() : m_handle{} {}

		CommandBuffer(CommandBufferHandle handle) : m_handle(handle) {}

		void Create(CommandPool& commandPool);
		
		void Destroy();

		void CopyBuffer(Buffer& srcBuffer, Buffer& dstBuffer, uint32_t regionCount, BufferCopy* pRegions);

		void Begin();

		void End();

		void PipelineBarrier();

		void SetDepthBias(float constantFactor, float clamp, float slopeFactor);

		void BlitImage(Image& srcImage, ImageLayout srcImageLayout,
			Image& dstImage, ImageLayout dstImageLayout,
			uint32_t regionCount, ImageBlit* pRegions,
			Filter filter);

		void BeginPass(RenderPass& pass, FrameBuffer& frameBuffer);

		void EndPass();

		void BindPipeline(Pipeline& pipeline);

		void BindComputePipeline(Pipeline& pipeline);

		void BindVertexBuffer(Buffer& buffer, size_t offset);

		void BindIndexBuffer(Buffer& buffer, size_t offset);

		void BindDescriptors(Pipeline& pipeline, uint32_t count, DescriptorSetHandle* descriptors);

		void BindComputeDescriptors(Pipeline& pipeline, uint32_t count, DescriptorSetHandle* descriptors);

		void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

		void PushConstants(Pipeline& pipeline, ShaderStageFlags shaderStageFlags, uint32_t offset, uint32_t size, const void* pValues);

		void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);

		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);

		void Submit();

		CommandBufferHandle& Handle() { return m_handle; }

	private:
		CommandBufferHandle m_handle;
	};
}
