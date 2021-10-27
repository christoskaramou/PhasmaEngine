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
#include "Renderer/Image.h"
#include "Renderer/RHI.h"
#include "ECS/Context.h"
#include "Renderer/CommandBuffer.h"
#include "Renderer/Buffer.h"

namespace pe
{
	Image::Image()
	{
		image = {};
		view = {};
		sampler = {};
		samples = VK_SAMPLE_COUNT_1_BIT;
		layoutState = LayoutState::ColorWrite;
		format = VK_FORMAT_UNDEFINED;
		initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		tiling = VK_IMAGE_TILING_OPTIMAL;
		mipLevels = 1;
		arrayLayers = 1;
		anisotropyEnabled = VK_TRUE;
		minLod = 0.f;
		maxLod = 1.f;
		maxAnisotropy = 16.f;
		filter = VK_FILTER_LINEAR;
		imageCreateFlags = VkImageCreateFlags();
		viewType = VK_IMAGE_VIEW_TYPE_2D;
		addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		samplerCompareEnable = VK_FALSE;
		compareOp = VK_COMPARE_OP_LESS;
		samplerMipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		blendAttachment = {};
	}

	Image::~Image()
	{
	}

	void Image::TransitionImageLayout(
		CommandBuffer cmd,
		ImageLayout oldLayout,
		ImageLayout newLayout,
		PipelineStageFlags oldStageMask,
		PipelineStageFlags newStageMask,
		AccessFlags srcMask,
		AccessFlags dstMask,
		ImageAspectFlags aspectFlags
	)
	{
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcAccessMask = srcMask;
		barrier.dstAccessMask = dstMask;
		barrier.image = image;
		barrier.oldLayout = (VkImageLayout)oldLayout;
		barrier.newLayout = (VkImageLayout)newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = aspectFlags;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = arrayLayers;
		if (format == VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT)
			barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

		vkCmdPipelineBarrier(
			cmd.Handle(),
			oldStageMask,
			newStageMask,
			VK_DEPENDENCY_BY_REGION_BIT,
			0, nullptr,
			0, nullptr,
			1, &barrier);
	}

	void Image::CreateImage(uint32_t width, uint32_t height, ImageTiling tiling, ImageUsageFlags usage, MemoryPropertyFlags properties)
	{
		auto _usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // All images can be copied

		VkFormatProperties fProps;
		vkGetPhysicalDeviceFormatProperties(RHII.gpu, (VkFormat)format, &fProps);

		if (tiling == VK_IMAGE_TILING_OPTIMAL)
		{
			if (!fProps.optimalTilingFeatures)
				throw std::runtime_error("createImage(): wrong format error, no optimal tiling features supported.");
		}
		else if (tiling == VK_IMAGE_TILING_LINEAR)
		{
			if (!fProps.linearTilingFeatures)
				throw std::runtime_error("createImage(): wrong format error, no linear tiling features supported.");
		}
		else
		{
			throw std::runtime_error("createImage(): wrong format error.");
		}

		VkImageFormatProperties ifProps;
		vkGetPhysicalDeviceImageFormatProperties(RHII.gpu, (VkFormat)format, VK_IMAGE_TYPE_2D, (VkImageTiling)tiling, _usage, VkImageCreateFlags(), &ifProps);

		if (ifProps.maxArrayLayers < arrayLayers ||
			ifProps.maxExtent.width < width ||
			ifProps.maxExtent.height < height ||
			ifProps.maxMipLevels < mipLevels ||
			!(ifProps.sampleCounts & samples))
			throw std::runtime_error("createImage(): image format properties error!");


		this->tiling = (VkImageTiling)tiling;
		this->width = width % 2 != 0 ? width - 1 : width;
		this->height = height % 2 != 0 ? height - 1 : height;
		width_f = static_cast<float>(this->width);
		height_f = static_cast<float>(this->height);

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.flags = imageCreateFlags;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = (VkFormat)format;
		imageInfo.extent = VkExtent3D{ this->width, this->height, 1 };
		imageInfo.mipLevels = mipLevels;
		imageInfo.arrayLayers = arrayLayers;
		imageInfo.samples = (VkSampleCountFlagBits)samples;
		imageInfo.tiling = (VkImageTiling)tiling;
		imageInfo.usage = _usage;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = (VkImageLayout)initialLayout;

		VmaAllocationCreateInfo allocationCreateInfo = {};
		allocationCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		VmaAllocationInfo allocationInfo;
		VkImage vkImage;
		vmaCreateImage(RHII.allocator, &imageInfo, &allocationCreateInfo, &vkImage, &allocation, &allocationInfo);
		image = vkImage;
	}

	void Image::CreateImageView(ImageAspectFlags aspectFlags)
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = (VkImageViewType)viewType;
		viewInfo.format = (VkFormat)format;
		viewInfo.subresourceRange = { (VkImageAspectFlags)aspectFlags, 0, mipLevels, 0, arrayLayers };

		VkImageView vkView;
		vkCreateImageView(RHII.device, &viewInfo, nullptr, &vkView);
		view = vkView;
	}

	void Image::TransitionImageLayout(ImageLayout oldLayout, ImageLayout newLayout)
	{
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;
		allocInfo.commandPool = RHII.commandPool2.Handle();

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(RHII.device, &allocInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = (VkImageLayout)oldLayout;
		barrier.newLayout = (VkImageLayout)newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;

		VkPipelineStageFlags srcStage;
		VkPipelineStageFlags dstStage;

		// Subresource aspectMask
		if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, mipLevels, 0, arrayLayers };
			if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT)
			{
				barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
			}
		}
		else barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, arrayLayers };

		// Src, Dst AccessMasks and Pipeline Stages for pipelineBarrier
		if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
			newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
			newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
		{
			barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
		{
			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
		{
			srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		}
		else
		{
			throw std::runtime_error("Transition image layout invalid combination of layouts");
		}

		vkCmdPipelineBarrier(
			commandBuffer,
			srcStage,
			dstStage,
			VK_DEPENDENCY_BY_REGION_BIT,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		vkEndCommandBuffer(commandBuffer);

		CommandBuffer cmdBuffer(commandBuffer);
		RHII.SubmitAndWaitFence(1, &cmdBuffer, nullptr, 0, nullptr, 0, nullptr);

		vkFreeCommandBuffers(RHII.device, RHII.commandPool2.Handle(), 1, &commandBuffer);
	}

	void Image::ChangeLayout(CommandBuffer cmd, LayoutState state)
	{
		if (state != layoutState)
		{
			if (state == LayoutState::ColorRead)
			{
				TransitionImageLayout(
					cmd,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT
				);
			}
			else if (state == LayoutState::ColorWrite)
			{
				TransitionImageLayout(
					cmd,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_ACCESS_SHADER_READ_BIT,
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					VK_IMAGE_ASPECT_COLOR_BIT
				);
			}
			else if (state == LayoutState::DepthRead)
			{
				TransitionImageLayout(
					cmd,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
					VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_ASPECT_DEPTH_BIT
				);
			}
			else if (state == LayoutState::DepthWrite)
			{
				TransitionImageLayout(
					cmd,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
					VK_ACCESS_SHADER_READ_BIT,
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					VK_IMAGE_ASPECT_DEPTH_BIT
				);
			}
			layoutState = state;
		}
	}

	void Image::CopyBufferToImage(Buffer* buffer, uint32_t baseLayer)
	{
		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;
		allocInfo.commandPool = RHII.commandPool2.Handle();

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers(RHII.device, &allocInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		VkBufferImageCopy region;
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = baseLayer;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = VkOffset3D{ 0, 0, 0 };
		region.imageExtent = VkExtent3D{ width, height, 1 };

		vkCmdCopyBufferToImage(commandBuffer, buffer->Handle(), image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		vkEndCommandBuffer(commandBuffer);

		CommandBuffer cmdBuffer(commandBuffer);
		RHII.SubmitAndWaitFence(1, &cmdBuffer, nullptr, 0, nullptr, 0, nullptr);

		vkFreeCommandBuffers(RHII.device, RHII.commandPool2.Handle(), 1, &commandBuffer);
	}

	void Image::CopyColorAttachment(CommandBuffer cmd, Image& renderedImage)
	{
		TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
		renderedImage.TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);

		// copy the image
		VkImageCopy region{};
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.srcSubresource.layerCount = 1;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.layerCount = 1;
		region.extent.width = renderedImage.width;
		region.extent.height = renderedImage.height;
		region.extent.depth = 1;

		vkCmdCopyImage(
			cmd.Handle(),
			renderedImage.image,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &region);

		TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
		renderedImage.TransitionImageLayout(
			cmd,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_ASPECT_COLOR_BIT
		);
	}

	void Image::GenerateMipMaps()
	{
		VkFormatProperties fProps;
		vkGetPhysicalDeviceFormatProperties(RHII.gpu, (VkFormat)format, &fProps);

		if (tiling == VK_IMAGE_TILING_OPTIMAL)
		{
			if (!(fProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
				throw std::runtime_error("generateMipMaps(): Image tiling error, linear filter is not supported.");
		}
		else if (tiling == VK_IMAGE_TILING_LINEAR)
		{
			if (!(fProps.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
				throw std::runtime_error("generateMipMaps(): Image tiling error, linear filter is not supported.");
		}
		else
		{
			throw std::runtime_error("generateMipMaps(): Image tiling error.");
		}

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = mipLevels;
		allocInfo.commandPool = RHII.commandPool2.Handle();

		std::vector<VkCommandBuffer> commandBuffers(mipLevels);
		vkAllocateCommandBuffers(RHII.device, &allocInfo, commandBuffers.data());

		auto mipWidth = static_cast<int32_t>(width);
		auto mipHeight = static_cast<int32_t>(height);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.image = image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.subresourceRange.levelCount = 1;

		VkImageBlit blit;
		blit.srcOffsets[0] = VkOffset3D{ 0, 0, 0 };
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = VkOffset3D{ 0, 0, 0 };
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		for (uint32_t i = 1; i < mipLevels; i++)
		{
			vkBeginCommandBuffer(commandBuffers[i], &beginInfo);

			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(
				commandBuffers[i],
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VkDependencyFlagBits(),
				0, nullptr,
				0, nullptr,
				1, &barrier);

			blit.srcOffsets[1] = VkOffset3D{ mipWidth, mipHeight, 1 };
			blit.dstOffsets[1] = VkOffset3D{ mipWidth / 2, mipHeight / 2, 1 };
			blit.srcSubresource.mipLevel = i - 1;
			blit.dstSubresource.mipLevel = i;

			vkCmdBlitImage(
				commandBuffers[i],
				image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit,
				VK_FILTER_LINEAR);

			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(
				commandBuffers[i],
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_DEPENDENCY_BY_REGION_BIT,
				0, nullptr,
				0, nullptr,
				1, &barrier);

			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;

			vkEndCommandBuffer(commandBuffers[i]);

			CommandBuffer cmdBuffer(commandBuffers[i]);
			RHII.SubmitAndWaitFence(1, &cmdBuffer, nullptr, 0, nullptr, 0, nullptr);
		}

		vkBeginCommandBuffer(commandBuffers[0], &beginInfo);

		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(
			commandBuffers[0],
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_DEPENDENCY_BY_REGION_BIT,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		vkEndCommandBuffer(commandBuffers[0]);

		CommandBuffer cmdBuffer(commandBuffers[0]);
		RHII.SubmitAndWaitFence(1, &cmdBuffer, nullptr, 0, nullptr, 0, nullptr);

		vkFreeCommandBuffers(RHII.device, RHII.commandPool2.Handle(), mipLevels, commandBuffers.data());
	}

	void Image::CreateSampler()
	{
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = (VkFilter)filter;
		samplerInfo.minFilter = (VkFilter)filter;
		samplerInfo.mipmapMode = (VkSamplerMipmapMode)samplerMipmapMode;
		samplerInfo.minLod = minLod;
		samplerInfo.maxLod = maxLod;
		samplerInfo.mipLodBias = 0.f;
		samplerInfo.addressModeU = (VkSamplerAddressMode)addressMode;
		samplerInfo.addressModeV = (VkSamplerAddressMode)addressMode;
		samplerInfo.addressModeW = (VkSamplerAddressMode)addressMode;
		samplerInfo.anisotropyEnable = anisotropyEnabled;
		samplerInfo.maxAnisotropy = maxAnisotropy;
		samplerInfo.compareEnable = samplerCompareEnable;
		samplerInfo.compareOp = (VkCompareOp)compareOp;
		samplerInfo.borderColor = (VkBorderColor)borderColor;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		VkSampler vkSampler;
		vkCreateSampler(RHII.device, &samplerInfo, nullptr, &vkSampler);
		sampler = vkSampler;
	}

	void Image::Destroy()
	{
		if (VkImageView(view)) vkDestroyImageView(RHII.device, view, nullptr);
		if (VkImage(image)) vmaDestroyImage(RHII.allocator, image, allocation);
		if (VkSampler(sampler)) vkDestroySampler(RHII.device, sampler, nullptr);
		view = {};
		image = {};
		sampler = {};
	}
}
#endif
