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
#include "Renderer/Pipeline.h"

namespace pe
{
	enum class LayoutState
	{
		ColorRead,
		ColorWrite,
		DepthRead,
		DepthWrite
	};

	class Context;
	class CommandBuffer;
	class Buffer;

	class Image
	{
	public:
		Image();
		
		~Image();

		std::string name{};

		ImageHandle image;
		ImageViewHandle view;
		SamplerHandle sampler;
		VmaAllocation allocation {};
		uint32_t width {};
		uint32_t height {};
		float width_f {};
		float height_f {};
		
		// values
		SampleCountFlagBits samples;
		LayoutState layoutState;
		Format format;
		ImageLayout initialLayout;
		ImageTiling tiling;
		uint32_t mipLevels;
		uint32_t arrayLayers;
		bool anisotropyEnabled;
		float minLod, maxLod, maxAnisotropy;
		Filter filter;
		ImageCreateFlags imageCreateFlags;
		ImageViewType viewType;
		SamplerAddressMode addressMode;
		BorderColor borderColor;
		bool samplerCompareEnable;
		CompareOp compareOp;
		SamplerMipmapMode samplerMipmapMode;
		PipelineColorBlendAttachmentState blendAttachment;
		
		void TransitionImageLayout(
			CommandBuffer cmd,
			ImageLayout oldLayout,
			ImageLayout newLayout,
			PipelineStageFlags oldStageMask,
			PipelineStageFlags newStageMask,
			AccessFlags srcMask,
			AccessFlags dstMask,
			ImageAspectFlags aspectFlags
		);
		
		void CreateImage(uint32_t width, uint32_t height, ImageTiling tiling, ImageUsageFlags usage, MemoryPropertyFlags properties);
		
		void CreateImageView(ImageAspectFlags aspectFlags);
		
		void TransitionImageLayout(ImageLayout oldLayout, ImageLayout newLayout);
		
		void ChangeLayout(CommandBuffer cmd, LayoutState state);
		
		void CopyBufferToImage(Buffer* buffer, uint32_t baseLayer = 0);
		
		void CopyColorAttachment(CommandBuffer cmd, Image& renderedImage);
		
		void GenerateMipMaps();
		
		void CreateSampler();
		
		void Destroy();
	};
}