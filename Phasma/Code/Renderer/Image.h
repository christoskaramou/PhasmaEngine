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
	enum class LayoutState
	{
		ColorRead,
		ColorWrite,
		DepthRead,
		DepthWrite
	};
	
	class Context;
	
	class Image
	{
	public:
		Image();
		
		~Image();

		std::string name{};

		SPtr<vk::Image> image;
		VmaAllocation allocation {};
		SPtr<vk::ImageView> view;
		SPtr<vk::Sampler> sampler;
		uint32_t width {};
		uint32_t height {};
		float width_f {};
		float height_f {};
		SPtr<vk::Extent2D> extent;
		
		// values
		SPtr<vk::SampleCountFlagBits> samples;
		LayoutState layoutState;
		SPtr<vk::Format> format;
		SPtr<vk::ImageLayout> initialLayout;
		SPtr<vk::ImageTiling> tiling;
		uint32_t mipLevels;
		uint32_t arrayLayers;
		bool anisotropyEnabled;
		float minLod, maxLod, maxAnisotropy;
		SPtr<vk::Filter> filter;
		SPtr<vk::ImageCreateFlags> imageCreateFlags;
		SPtr<vk::ImageViewType> viewType;
		SPtr<vk::SamplerAddressMode> addressMode;
		SPtr<vk::BorderColor> borderColor;
		bool samplerCompareEnable;
		SPtr<vk::CompareOp> compareOp;
		SPtr<vk::SamplerMipmapMode> samplerMipmapMode;
		SPtr<vk::PipelineColorBlendAttachmentState> blentAttachment;
		
		void transitionImageLayout(
				vk::CommandBuffer cmd,
				vk::ImageLayout oldLayout,
				vk::ImageLayout newLayout,
				const vk::PipelineStageFlags& oldStageMask,
				const vk::PipelineStageFlags& newStageMask,
				const vk::AccessFlags& srcMask,
				const vk::AccessFlags& dstMask,
				const vk::ImageAspectFlags& aspectFlags
		) const;
		
		void createImage(
				uint32_t width, uint32_t height, vk::ImageTiling tiling, const vk::ImageUsageFlags& usage,
				const vk::MemoryPropertyFlags& properties
		);
		
		void createImageView(const vk::ImageAspectFlags& aspectFlags);
		
		void transitionImageLayout(vk::ImageLayout oldLayout, vk::ImageLayout newLayout) const;
		
		void changeLayout(const vk::CommandBuffer& cmd, LayoutState state);
		
		void copyBufferToImage(vk::Buffer buffer, uint32_t baseLayer = 0) const;
		
		void copyColorAttachment(const vk::CommandBuffer& cmd, Image& renderedImage) const;
		
		void generateMipMaps() const;
		
		void createSampler();
		
		void destroy() const;

		void SetDebugName(const std::string& debugName);
	};
}