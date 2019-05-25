#include "Image.h"

using namespace vm;

void Image::transitionImageLayout(const vk::CommandBuffer cmd, const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout, const vk::PipelineStageFlags oldStageMask, const vk::PipelineStageFlags newStageMask, const vk::ImageAspectFlags aspectFlags)
{
	vk::ImageMemoryBarrier barrier;
	barrier.image = image;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange = { aspectFlags, 0, mipLevels, 0, arrayLayers };
	if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint) {
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

void Image::createImage(const uint32_t width, const uint32_t height, const vk::ImageTiling tiling, const vk::ImageUsageFlags usage, const vk::MemoryPropertyFlags properties, vk::SampleCountFlagBits samples)
{
	vk::ImageCreateInfo imageInfo;
	imageInfo.flags = imageCreateFlags;
	imageInfo.imageType = vk::ImageType::e2D;
	imageInfo.format = format;
	imageInfo.extent = { width, height, 1 };
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = arrayLayers;
	imageInfo.samples = samples;
	imageInfo.tiling = tiling;
	imageInfo.usage = usage;
	imageInfo.sharingMode = vk::SharingMode::eExclusive;
	imageInfo.initialLayout = initialLayout;
	//vk::ImageFormatProperties imageFormatProperties = vulkan->gpu.getImageFormatProperties(format, vk::ImageType::e2D, tiling, usage, vk::ImageCreateFlags());

	image = vulkan->device.createImage(imageInfo);

	uint32_t memTypeIndex = UINT32_MAX;
	auto const memRequirements = vulkan->device.getImageMemoryRequirements(image);
	auto const memProperties = vulkan->gpu.getMemoryProperties();
	for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
		if (memRequirements.memoryTypeBits & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			memTypeIndex = i;
			break;
		}
	}
	if (memTypeIndex == UINT32_MAX)
	{
		for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
			if (memRequirements.memoryTypeBits & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) == vk::MemoryPropertyFlagBits::eDeviceLocal) {
				memTypeIndex = i;
				break;
			}
		}
	}

	vk::MemoryAllocateInfo allocInfo;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = memTypeIndex;

	memory = vulkan->device.allocateMemory(allocInfo);
	vulkan->device.bindImageMemory(image, memory, 0);
}

void Image::createImageView(const vk::ImageAspectFlags aspectFlags)
{
	vk::ImageViewCreateInfo viewInfo;
	viewInfo.image = image;
	viewInfo.viewType = viewType;
	viewInfo.format = format;
	viewInfo.subresourceRange = { aspectFlags, 0, mipLevels, 0, arrayLayers };

	view = vulkan->device.createImageView(viewInfo);
}

void Image::transitionImageLayout(const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout)
{
	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	allocInfo.commandBufferCount = 1;
	allocInfo.commandPool = vulkan->commandPool2;

	vk::CommandBuffer commandBuffer;
	commandBuffer = vulkan->device.allocateCommandBuffers(allocInfo).at(0);

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	commandBuffer.begin(beginInfo);

	vk::ImageMemoryBarrier barrier;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;

	vk::PipelineStageFlags srcStage;
	vk::PipelineStageFlags dstStage;

	// Subresource aspectMask
	if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
		barrier.subresourceRange = { vk::ImageAspectFlagBits::eDepth, 0, mipLevels, 0, arrayLayers };
		if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint) {
			barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
		}
	}
	else barrier.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, arrayLayers };

	// Src, Dst AccessMasks and Pipeline Stages for pipelineBarrier
	if (oldLayout == vk::ImageLayout::ePreinitialized && newLayout == vk::ImageLayout::eTransferSrcOptimal) {
		barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		dstStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (oldLayout == vk::ImageLayout::ePreinitialized && newLayout == vk::ImageLayout::eTransferDstOptimal) {
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		dstStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		dstStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		srcStage = vk::PipelineStageFlagBits::eTransfer;
		dstStage = vk::PipelineStageFlagBits::eFragmentShader;
	}
	else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		dstStage = vk::PipelineStageFlagBits::eFragmentShader;
	}
	else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
		barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;

		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		dstStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
	}
	else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		dstStage = vk::PipelineStageFlagBits::eFragmentShader;
	}
	else {
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

	vk::FenceCreateInfo fi;
	vk::Fence fence = vulkan->device.createFence(fi);

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	vulkan->graphicsQueue.submit(submitInfo, fence);

	vulkan->device.waitForFences(fence, VK_TRUE, UINT64_MAX);
	vulkan->device.resetFences(fence);

	vulkan->device.destroyFence(fence);
	vulkan->device.freeCommandBuffers(vulkan->commandPool2, commandBuffer);
}

void Image::copyBufferToImage(const vk::Buffer buffer, const int x, const int y, const int width, const int height, const uint32_t baseLayer)
{
	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	allocInfo.commandBufferCount = 1;
	allocInfo.commandPool = vulkan->commandPool2;

	vk::CommandBuffer commandBuffer = vulkan->device.allocateCommandBuffers(allocInfo).at(0);

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
	region.imageOffset = vk::Offset3D(x, y, 0);
	region.imageExtent = vk::Extent3D(static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1);

	commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, 1, &region);

	commandBuffer.end();

	vk::FenceCreateInfo fi;
	vk::Fence fence = vulkan->device.createFence(fi);

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	vulkan->graphicsQueue.submit(submitInfo, fence);

	vulkan->device.waitForFences(fence, VK_TRUE, UINT64_MAX);
	vulkan->device.resetFences(fence);

	vulkan->device.destroyFence(fence);
	vulkan->device.freeCommandBuffers(vulkan->commandPool2, commandBuffer);
}

void Image::generateMipMaps(const int32_t texWidth, const int32_t texHeight)
{
	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	allocInfo.commandBufferCount = 1;
	allocInfo.commandPool = vulkan->commandPool2;

	vk::CommandBuffer commandBuffer = vulkan->device.allocateCommandBuffers(allocInfo).at(0);

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	commandBuffer.begin(beginInfo);

	vk::ImageMemoryBarrier barrier = {};
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = texWidth;
	int32_t mipHeight = texHeight;

	for (uint32_t i = 1; i < mipLevels; i++) {
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

		commandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
			vk::DependencyFlagBits(),
			nullptr,
			nullptr,
			barrier);

		vk::ImageBlit blit = {};
		blit.srcOffsets[0] = { 0, 0, 0 };
		blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
		blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = { 0, 0, 0 };
		blit.dstOffsets[1] = { mipWidth / 2, mipHeight / 2, 1 };
		blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		commandBuffer.blitImage(
			image,
			vk::ImageLayout::eTransferSrcOptimal,
			image,
			vk::ImageLayout::eTransferDstOptimal,
			blit,
			vk::Filter::eLinear
		);

		barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		commandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eFragmentShader,
			vk::DependencyFlagBits::eByRegion,
			nullptr,
			nullptr,
			barrier
		);

		if (mipWidth > 1) mipWidth /= 2;
		if (mipHeight > 1) mipHeight /= 2;
	}

	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

	commandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer,
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::DependencyFlagBits::eByRegion,
		nullptr,
		nullptr,
		barrier
	);

	commandBuffer.end();

	vk::FenceCreateInfo fi;
	vk::Fence fence = vulkan->device.createFence(fi);

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	vulkan->graphicsQueue.submit(submitInfo, fence);

	vulkan->device.waitForFences(fence, VK_TRUE, UINT64_MAX);
	vulkan->device.resetFences(fence);

	vulkan->device.destroyFence(fence);
	vulkan->device.freeCommandBuffers(vulkan->commandPool2, commandBuffer);
}

void Image::createSampler()
{
	vk::SamplerCreateInfo samplerInfo;
	samplerInfo.magFilter = filter;
	samplerInfo.minFilter = filter;
	samplerInfo.mipmapMode = samplerMipmapMode;
	samplerInfo.minLod = minLod;
	samplerInfo.maxLod = maxLod;
	samplerInfo.mipLodBias = 0.f;
	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.anisotropyEnable = anisotropyEnabled;
	samplerInfo.maxAnisotropy = maxAnisotropy;
	samplerInfo.compareEnable = samplerCompareEnable;
	samplerInfo.compareOp = compareOp;
	samplerInfo.borderColor = borderColor;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	sampler = vulkan->device.createSampler(samplerInfo);
}

void Image::destroy()
{
	if (view) vulkan->device.destroyImageView(view);
	if (image) vulkan->device.destroyImage(image);
	if (memory) vulkan->device.freeMemory(memory);
	if (sampler) vulkan->device.destroySampler(sampler);
	view = nullptr;
	image = nullptr;
	memory = nullptr;
	sampler = nullptr;
}