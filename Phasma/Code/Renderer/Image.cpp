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

#include "Image.h"
#include "RenderApi.h"
#include "../ECS/Context.h"
#include <utility>

namespace pe
{
	Image::Image()
	{
		samples = make_ref(vk::SampleCountFlagBits::e1);
		layoutState = LayoutState::ColorWrite;
		format = make_ref(vk::Format::eUndefined);
		initialLayout = make_ref(vk::ImageLayout::ePreinitialized);
		tiling = make_ref(vk::ImageTiling::eOptimal);
		mipLevels = 1;
		arrayLayers = 1;
		anisotropyEnabled = VK_TRUE;
		minLod = 0.f;
		maxLod = 1.f;
		maxAnisotropy = 16.f;
		filter = make_ref(vk::Filter::eLinear);
		imageCreateFlags = make_ref(vk::ImageCreateFlags());
		viewType = make_ref(vk::ImageViewType::e2D);
		addressMode = make_ref(vk::SamplerAddressMode::eRepeat);
		borderColor = make_ref(vk::BorderColor::eFloatOpaqueBlack);
		samplerCompareEnable = VK_FALSE;
		compareOp = make_ref(vk::CompareOp::eLess);
		samplerMipmapMode = make_ref(vk::SamplerMipmapMode::eLinear);
		blentAttachment = make_ref(vk::PipelineColorBlendAttachmentState());
	}
	
	Image::~Image()
	{
	}
	
	void Image::transitionImageLayout(
			vk::CommandBuffer cmd,
			vk::ImageLayout oldLayout,
			vk::ImageLayout newLayout,
			const vk::PipelineStageFlags& oldStageMask,
			const vk::PipelineStageFlags& newStageMask,
			const vk::AccessFlags& srcMask,
			const vk::AccessFlags& dstMask,
			const vk::ImageAspectFlags& aspectFlags
	) const
	{
		vk::ImageMemoryBarrier barrier;
		barrier.srcAccessMask = srcMask;
		barrier.dstAccessMask = dstMask;
		barrier.image = *image;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = aspectFlags;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = arrayLayers;
		if (*format == vk::Format::eD32SfloatS8Uint || *format == vk::Format::eD24UnormS8Uint)
		{
			barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
		}
		
		cmd.pipelineBarrier(
				oldStageMask,
				newStageMask,
				vk::DependencyFlagBits::eByRegion,
				nullptr,
				nullptr,
				barrier
		);
	}
	
	void Image::createImage(
			uint32_t width, uint32_t height, vk::ImageTiling tiling, const vk::ImageUsageFlags& usage,
			const vk::MemoryPropertyFlags& properties
	)
	{
		auto vCtx = VulkanContext::Get();
		
		const auto fProps = vCtx->gpu->getFormatProperties(*format);
		
		if (tiling == vk::ImageTiling::eOptimal)
		{
			if (!fProps.optimalTilingFeatures)
				throw std::runtime_error("createImage(): wrong format error, no optimal tiling features supported.");
		}
		else if (tiling == vk::ImageTiling::eLinear)
		{
			if (!fProps.linearTilingFeatures)
				throw std::runtime_error("createImage(): wrong format error, no linear tiling features supported.");
		}
		else
		{
			throw std::runtime_error("createImage(): wrong format error.");
		}
		
		auto const ifProps = vCtx->gpu
		                         ->getImageFormatProperties(
				                         *format, vk::ImageType::e2D, tiling, usage, vk::ImageCreateFlags());
		if (ifProps.maxArrayLayers < arrayLayers ||
		    ifProps.maxExtent.width < width ||
		    ifProps.maxExtent.height < height ||
		    ifProps.maxMipLevels < mipLevels ||
		    !(ifProps.sampleCounts & *samples))
			throw std::runtime_error("createImage(): image format properties error!");
		
		
		this->tiling = make_ref(tiling);
		this->width = width % 2 != 0 ? width - 1 : width;
		this->height = height % 2 != 0 ? height - 1 : height;
		width_f = static_cast<float>(this->width);
		height_f = static_cast<float>(this->height);
		extent = make_ref(vk::Extent2D {this->width, this->height});
		
		vk::ImageCreateInfo imageInfo;
		imageInfo.flags = *imageCreateFlags;
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = *format;
		imageInfo.extent = vk::Extent3D {this->width, this->height, 1};
		imageInfo.mipLevels = mipLevels;
		imageInfo.arrayLayers = arrayLayers;
		imageInfo.samples = *samples;
		imageInfo.tiling = tiling;
		imageInfo.usage = usage;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;
		imageInfo.initialLayout = *initialLayout;
		VkImageCreateInfo vkImageInfo = VkImageCreateInfo(imageInfo);
		
		VmaAllocationCreateInfo allocationCreateInfo = {};
		allocationCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		
		VkImage vkImage;
		VmaAllocationInfo allocationInfo;
		vmaCreateImage(
				VulkanContext::Get()->allocator, &vkImageInfo, &allocationCreateInfo, &vkImage, &allocation,
				&allocationInfo
		);
		image = make_ref(vk::Image(vkImage));
		
		vCtx->SetDebugObjectName(*image, "");
	}
	
	void Image::createImageView(const vk::ImageAspectFlags& aspectFlags)
	{
		auto vCtx = VulkanContext::Get();
		
		vk::ImageViewCreateInfo viewInfo;
		viewInfo.image = *image;
		viewInfo.viewType = *viewType;
		viewInfo.format = *format;
		viewInfo.subresourceRange = {aspectFlags, 0, mipLevels, 0, arrayLayers};
		
		view = make_ref(vCtx->device->createImageView(viewInfo));
		
		vCtx->SetDebugObjectName(*view, "");
	}
	
	void Image::transitionImageLayout(const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout) const
	{
		auto vCtx = VulkanContext::Get();
		
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;
		allocInfo.commandPool = *vCtx->commandPool2;
		
		const vk::CommandBuffer commandBuffer = vCtx->device->allocateCommandBuffers(allocInfo).at(0);
		
		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		commandBuffer.begin(beginInfo);
		
		vk::ImageMemoryBarrier barrier;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = *image;
		
		vk::PipelineStageFlags srcStage;
		vk::PipelineStageFlags dstStage;
		
		// Subresource aspectMask
		if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
		{
			barrier.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, mipLevels, 0, arrayLayers};
			if (*format == vk::Format::eD32SfloatS8Uint || *format == vk::Format::eD24UnormS8Uint)
			{
				barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
			}
		}
		else barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, arrayLayers};
		
		// Src, Dst AccessMasks and Pipeline Stages for pipelineBarrier
		if (oldLayout == vk::ImageLayout::ePreinitialized && newLayout == vk::ImageLayout::eTransferSrcOptimal)
		{
			barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			
			srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
			dstStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == vk::ImageLayout::ePreinitialized && newLayout == vk::ImageLayout::eTransferDstOptimal)
		{
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			
			srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
			dstStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
		{
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
			
			srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
			dstStage = vk::PipelineStageFlagBits::eTransfer;
		}
		else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
		         newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
			
			srcStage = vk::PipelineStageFlagBits::eTransfer;
			dstStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
		{
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
			
			srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
			dstStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == vk::ImageLayout::eUndefined &&
		         newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal)
		{
			barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
			
			srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
			dstStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
		}
		else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eColorAttachmentOptimal)
		{
			srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
			dstStage = vk::PipelineStageFlagBits::eFragmentShader;
		}
		else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::ePresentSrcKHR)
		{
			srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
			dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		}
		else
		{
			throw std::runtime_error("Transition image layout invalid combination of layouts");
		}
		
		commandBuffer.pipelineBarrier(
				srcStage,
				dstStage,
				vk::DependencyFlagBits::eByRegion,
				nullptr,
				nullptr,
				barrier
		);
		
		commandBuffer.end();
		
		vCtx->submitAndWaitFence(commandBuffer, nullptr, nullptr, nullptr);
		
		vCtx->device->freeCommandBuffers(*vCtx->commandPool2, commandBuffer);
	}
	
	void Image::changeLayout(const vk::CommandBuffer& cmd, LayoutState state)
	{
		if (state != layoutState)
		{
			if (state == LayoutState::ColorRead)
			{
				transitionImageLayout(
						cmd,
						vk::ImageLayout::eColorAttachmentOptimal,
						vk::ImageLayout::eShaderReadOnlyOptimal,
						vk::PipelineStageFlagBits::eColorAttachmentOutput,
						vk::PipelineStageFlagBits::eFragmentShader,
						vk::AccessFlagBits::eColorAttachmentWrite,
						vk::AccessFlagBits::eShaderRead,
						vk::ImageAspectFlagBits::eColor
				);
			}
			else if (state == LayoutState::ColorWrite)
			{
				transitionImageLayout(
						cmd,
						vk::ImageLayout::eShaderReadOnlyOptimal,
						vk::ImageLayout::eColorAttachmentOptimal,
						vk::PipelineStageFlagBits::eFragmentShader,
						vk::PipelineStageFlagBits::eColorAttachmentOutput,
						vk::AccessFlagBits::eShaderRead,
						vk::AccessFlagBits::eColorAttachmentWrite,
						vk::ImageAspectFlagBits::eColor
				);
			}
			else if (state == LayoutState::DepthRead)
			{
				transitionImageLayout(
						cmd,
						vk::ImageLayout::eDepthStencilAttachmentOptimal,
						vk::ImageLayout::eDepthStencilReadOnlyOptimal,
						vk::PipelineStageFlagBits::eEarlyFragmentTests,
						vk::PipelineStageFlagBits::eFragmentShader,
						vk::AccessFlagBits::eDepthStencilAttachmentWrite,
						vk::AccessFlagBits::eShaderRead,
						vk::ImageAspectFlagBits::eDepth
				);
			}
			else if (state == LayoutState::DepthWrite)
			{
				transitionImageLayout(
						cmd,
						vk::ImageLayout::eDepthStencilReadOnlyOptimal,
						vk::ImageLayout::eDepthStencilAttachmentOptimal,
						vk::PipelineStageFlagBits::eFragmentShader,
						vk::PipelineStageFlagBits::eEarlyFragmentTests,
						vk::AccessFlagBits::eShaderRead,
						vk::AccessFlagBits::eDepthStencilAttachmentWrite,
						vk::ImageAspectFlagBits::eDepth
				);
			}
			layoutState = state;
		}
	}
	
	void Image::copyBufferToImage(const vk::Buffer buffer, const uint32_t baseLayer) const
	{
		auto vCtx = VulkanContext::Get();
		
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = 1;
		allocInfo.commandPool = *vCtx->commandPool2;
		
		const vk::CommandBuffer commandBuffer = vCtx->device->allocateCommandBuffers(allocInfo).at(0);
		
		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		commandBuffer.begin(beginInfo);
		
		vk::BufferImageCopy region;
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = baseLayer;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = vk::Offset3D(0, 0, 0);
		region.imageExtent = vk::Extent3D(width, height, 1);
		
		commandBuffer.copyBufferToImage(buffer, *image, vk::ImageLayout::eTransferDstOptimal, 1, &region);
		
		commandBuffer.end();
		
		vCtx->submitAndWaitFence(commandBuffer, nullptr, nullptr, nullptr);
		
		vCtx->device->freeCommandBuffers(*vCtx->commandPool2, commandBuffer);
	}
	
	void Image::copyColorAttachment(const vk::CommandBuffer& cmd, Image& renderedImage) const
	{
		transitionImageLayout(
				cmd,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::ImageLayout::eTransferDstOptimal,
				vk::PipelineStageFlagBits::eFragmentShader,
				vk::PipelineStageFlagBits::eTransfer,
				vk::AccessFlagBits::eShaderRead,
				vk::AccessFlagBits::eTransferWrite,
				vk::ImageAspectFlagBits::eColor
		);
		renderedImage.transitionImageLayout(
				cmd,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::ImageLayout::eTransferSrcOptimal,
				vk::PipelineStageFlagBits::eColorAttachmentOutput,
				vk::PipelineStageFlagBits::eTransfer,
				vk::AccessFlagBits::eColorAttachmentWrite,
				vk::AccessFlagBits::eTransferRead,
				vk::ImageAspectFlagBits::eColor
		);
		
		// copy the image
		vk::ImageCopy region;
		region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.dstSubresource.layerCount = 1;
		region.extent.width = renderedImage.width;
		region.extent.height = renderedImage.height;
		region.extent.depth = 1;
		
		cmd.copyImage(
				*renderedImage.image,
				vk::ImageLayout::eTransferSrcOptimal,
				*image,
				vk::ImageLayout::eTransferDstOptimal,
				region
		);
		
		transitionImageLayout(
				cmd,
				vk::ImageLayout::eTransferDstOptimal,
				vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eFragmentShader,
				vk::AccessFlagBits::eTransferWrite,
				vk::AccessFlagBits::eShaderRead,
				vk::ImageAspectFlagBits::eColor
		);
		renderedImage.transitionImageLayout(
				cmd,
				vk::ImageLayout::eTransferSrcOptimal,
				vk::ImageLayout::eColorAttachmentOptimal,
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eColorAttachmentOutput,
				vk::AccessFlagBits::eTransferRead,
				vk::AccessFlagBits::eColorAttachmentWrite,
				vk::ImageAspectFlagBits::eColor
		);
	}
	
	void Image::generateMipMaps() const
	{
		auto vCtx = VulkanContext::Get();
		
		const auto fProps = vCtx->gpu->getFormatProperties(*format);
		
		if (*tiling == vk::ImageTiling::eOptimal)
		{
			if (!(fProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
				throw std::runtime_error("generateMipMaps(): Image tiling error, linear filter is not supported.");
		}
		else if (*tiling == vk::ImageTiling::eLinear)
		{
			if (!(fProps.linearTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
				throw std::runtime_error("generateMipMaps(): Image tiling error, linear filter is not supported.");
		}
		else
		{
			throw std::runtime_error("generateMipMaps(): Image tiling error.");
		}
		
		vk::CommandBufferAllocateInfo allocInfo;
		allocInfo.level = vk::CommandBufferLevel::ePrimary;
		allocInfo.commandBufferCount = mipLevels;
		allocInfo.commandPool = *vCtx->commandPool2;
		
		const auto commandBuffers = vCtx->device->allocateCommandBuffers(allocInfo);
		
		auto mipWidth = static_cast<int32_t>(width);
		auto mipHeight = static_cast<int32_t>(height);
		
		vk::CommandBufferBeginInfo beginInfo;
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		
		vk::ImageMemoryBarrier barrier = {};
		barrier.image = *image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;
		
		vk::ImageBlit blit = {};
		blit.srcOffsets[0] = vk::Offset3D {0, 0, 0};
		blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = vk::Offset3D {0, 0, 0};
		blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;
		
		for (uint32_t i = 1; i < mipLevels; i++)
		{
			
			commandBuffers[i].begin(beginInfo);
			
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			
			commandBuffers[i].pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer,
					vk::PipelineStageFlagBits::eTransfer,
					vk::DependencyFlagBits(),
					nullptr,
					nullptr,
					barrier
			);
			
			
			blit.srcOffsets[1] = vk::Offset3D {mipWidth, mipHeight, 1};
			blit.dstOffsets[1] = vk::Offset3D {mipWidth / 2, mipHeight / 2, 1};
			blit.srcSubresource.mipLevel = i - 1;
			blit.dstSubresource.mipLevel = i;
			
			commandBuffers[i].blitImage(
					*image,
					vk::ImageLayout::eTransferSrcOptimal,
					*image,
					vk::ImageLayout::eTransferDstOptimal,
					blit,
					vk::Filter::eLinear
			);
			
			barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
			barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
			
			commandBuffers[i].pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer,
					vk::PipelineStageFlagBits::eFragmentShader,
					vk::DependencyFlagBits::eByRegion,
					nullptr,
					nullptr,
					barrier
			);
			
			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
			
			commandBuffers[i].end();
			
			vCtx->submitAndWaitFence(commandBuffers[i], nullptr, nullptr, nullptr);
		}
		
		commandBuffers.front().begin(beginInfo);
		
		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		
		commandBuffers.front().pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eFragmentShader,
				vk::DependencyFlagBits::eByRegion,
				nullptr,
				nullptr,
				barrier
		);
		
		commandBuffers.front().end();
		
		vCtx->submitAndWaitFence(commandBuffers.front(), nullptr, nullptr, nullptr);
		
		vCtx->device->freeCommandBuffers(*vCtx->commandPool2, commandBuffers);
	}
	
	void Image::createSampler()
	{
		auto vCtx = VulkanContext::Get();
		
		vk::SamplerCreateInfo samplerInfo;
		samplerInfo.magFilter = *filter;
		samplerInfo.minFilter = *filter;
		samplerInfo.mipmapMode = *samplerMipmapMode;
		samplerInfo.minLod = minLod;
		samplerInfo.maxLod = maxLod;
		samplerInfo.mipLodBias = 0.f;
		samplerInfo.addressModeU = *addressMode;
		samplerInfo.addressModeV = *addressMode;
		samplerInfo.addressModeW = *addressMode;
		samplerInfo.anisotropyEnable = anisotropyEnabled;
		samplerInfo.maxAnisotropy = maxAnisotropy;
		samplerInfo.compareEnable = samplerCompareEnable;
		samplerInfo.compareOp = *compareOp;
		samplerInfo.borderColor = *borderColor;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		sampler = make_ref(vCtx->device->createSampler(samplerInfo));
	}
	
	void Image::destroy() const
	{
		auto vCtx = VulkanContext::Get();
		
		if (*view) vCtx->device->destroyImageView(*view);
		if (*image) vmaDestroyImage(VulkanContext::Get()->allocator, VkImage(*image), allocation);
		if (*sampler) vCtx->device->destroySampler(*sampler);
		*view = nullptr;
		*image = nullptr;
		*sampler = nullptr;
	}
}
