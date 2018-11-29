#include "../include/Image.h"
#include "../include/Errors.h"

using namespace vm;

void Image::createImage(const uint32_t width, const uint32_t height, const vk::ImageTiling tiling, const vk::ImageUsageFlags usage, const vk::MemoryPropertyFlags properties, vk::SampleCountFlagBits samples)
{
	auto const imageInfo = vk::ImageCreateInfo()
		.setFlags(imageCreateFlags)
		.setImageType(vk::ImageType::e2D)
		.setFormat(format)
		.setExtent({ width, height, 1 })
		.setMipLevels(mipLevels)
		.setArrayLayers(arrayLayers)
		.setSamples(samples)
		.setTiling(tiling)
		.setUsage(usage)
		.setSharingMode(vk::SharingMode::eExclusive)
		.setInitialLayout(initialLayout);
	vk::ImageFormatProperties imageFormatProperties;
	vulkan->gpu.getImageFormatProperties(format, vk::ImageType::e2D, tiling, usage, vk::ImageCreateFlags(), &imageFormatProperties);

	VkCheck(vulkan->device.createImage(&imageInfo, nullptr, &image));

	uint32_t memTypeIndex = UINT32_MAX; ///////////////////
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
		//std::cout << "No suitable memory type found\n";
		//exit(-1);
	}

	auto const allocInfo = vk::MemoryAllocateInfo()
		.setAllocationSize(memRequirements.size)
		.setMemoryTypeIndex(memTypeIndex);

	VkCheck(vulkan->device.allocateMemory(&allocInfo, nullptr, &memory));
	VkCheck(vulkan->device.bindImageMemory(image, memory, 0));

	if (image && memory)
		std::cout << "Image created\n";
	else
	{
		std::cout << "Error at image creation\n";
		exit(-1);
	}
}

void Image::createImageView(const vk::ImageAspectFlags aspectFlags)
{
	auto const viewInfo = vk::ImageViewCreateInfo()
		.setImage(image)
		.setViewType(viewType)
		.setFormat(format)
		.setSubresourceRange({ aspectFlags, 0, mipLevels, 0, arrayLayers });

	VkCheck(vulkan->device.createImageView(&viewInfo, nullptr, &view));
	std::cout << "ImageView created\n";
}

void Image::transitionImageLayout(const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout)
{
	auto const allocInfo = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1)
		.setCommandPool(vulkan->commandPool);

	vk::CommandBuffer commandBuffer;
	VkCheck(vulkan->device.allocateCommandBuffers(&allocInfo, &commandBuffer));

	auto const beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	VkCheck(commandBuffer.begin(&beginInfo));

	auto barrier = vk::ImageMemoryBarrier()
		.setOldLayout(oldLayout)
		.setNewLayout(newLayout)
		.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setImage(image);

	vk::PipelineStageFlags srcStage;
	vk::PipelineStageFlags dstStage;

	// Subresource aspectMask
	if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
		barrier.setSubresourceRange({ vk::ImageAspectFlagBits::eDepth, 0, mipLevels, 0, arrayLayers });
		if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint) {
			barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
		}
	}
	else barrier.setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, arrayLayers });

	// Src, Dst AccessMasks and Pipeline Stages for pipelineBarrier
	if (oldLayout == vk::ImageLayout::ePreinitialized && newLayout == vk::ImageLayout::eTransferSrcOptimal) {
		barrier
			.setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
			.setDstAccessMask(vk::AccessFlagBits::eTransferRead);

		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		dstStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (oldLayout == vk::ImageLayout::ePreinitialized && newLayout == vk::ImageLayout::eTransferDstOptimal) {
		barrier
			.setSrcAccessMask(vk::AccessFlags())
			.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);

		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		dstStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal) {
		barrier
			.setSrcAccessMask(vk::AccessFlags())
			.setDstAccessMask(vk::AccessFlagBits::eTransferWrite);

		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		dstStage = vk::PipelineStageFlagBits::eTransfer;
	}
	else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
		barrier
			.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
			.setDstAccessMask(vk::AccessFlagBits::eShaderRead);

		srcStage = vk::PipelineStageFlagBits::eTransfer;
		dstStage = vk::PipelineStageFlagBits::eFragmentShader;
	}
	else if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
		barrier
			.setSrcAccessMask(vk::AccessFlags())
			.setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite);

		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
		dstStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
	}
	else {
		std::cout << "Not all image layout transitions are supported yet\n";
		exit(-1);
	}

	commandBuffer.pipelineBarrier(
		srcStage, dstStage,
		vk::DependencyFlags(),
		0, nullptr,
		0, nullptr,
		1, &barrier);

	VkCheck(commandBuffer.end());

	auto const submitInfo = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&commandBuffer);
	VkCheck(vulkan->graphicsQueue.submit(1, &submitInfo, nullptr));

	VkCheck(vulkan->graphicsQueue.waitIdle());
}

void Image::copyBufferToImage(const vk::Buffer buffer, const int x, const int y, const int width, const int height, const uint32_t baseLayer)
{
	auto const allocInfo = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1)
		.setCommandPool(vulkan->commandPool);

	vk::CommandBuffer commandBuffer;
	VkCheck(vulkan->device.allocateCommandBuffers(&allocInfo, &commandBuffer));

	auto const beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	VkCheck(commandBuffer.begin(&beginInfo));

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

	VkCheck(commandBuffer.end());

	auto const submitInfo = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&commandBuffer);
	VkCheck(vulkan->graphicsQueue.submit(1, &submitInfo, nullptr));

	VkCheck(vulkan->graphicsQueue.waitIdle());
}

void Image::generateMipMaps(const int32_t texWidth, const int32_t texHeight)
{
	auto const allocInfo = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1)
		.setCommandPool(vulkan->commandPool);

	vk::CommandBuffer commandBuffer;
	VkCheck(vulkan->device.allocateCommandBuffers(&allocInfo, &commandBuffer));

	auto const beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	VkCheck(commandBuffer.begin(&beginInfo));

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
			0, nullptr,
			0, nullptr,
			1, &barrier);

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
			image, vk::ImageLayout::eTransferSrcOptimal,
			image, vk::ImageLayout::eTransferDstOptimal,
			1, &blit,
			vk::Filter::eLinear);

		barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

		commandBuffer.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
			vk::DependencyFlagBits(),
			0, nullptr,
			0, nullptr,
			1, &barrier);

		if (mipWidth > 1) mipWidth /= 2;
		if (mipHeight > 1) mipHeight /= 2;
	}

	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

	commandBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
		vk::DependencyFlagBits(),
		0, nullptr,
		0, nullptr,
		1, &barrier);

	VkCheck(commandBuffer.end());

	auto const submitInfo = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&commandBuffer);
	VkCheck(vulkan->graphicsQueue.submit(1, &submitInfo, nullptr));

	VkCheck(vulkan->graphicsQueue.waitIdle());
}

void Image::createSampler()
{
	auto const samplerInfo = vk::SamplerCreateInfo()
		.setMagFilter(filter)
		.setMinFilter(filter)
		.setMipmapMode(vk::SamplerMipmapMode::eLinear)
		.setMinLod(minLod)
		.setMaxLod(maxLod)
		.setMipLodBias(0.f)
		.setAddressModeU(addressMode)
		.setAddressModeV(addressMode)
		.setAddressModeW(addressMode)
		.setAnisotropyEnable(VK_TRUE)
		.setMaxAnisotropy(maxAnisotropy)
		.setCompareEnable(samplerCompareEnable)
		.setCompareOp(vk::CompareOp::eLess)
		.setBorderColor(borderColor)
		.setUnnormalizedCoordinates(VK_FALSE);
	VkCheck(vulkan->device.createSampler(&samplerInfo, nullptr, &sampler));
	std::cout << "Image sampler created\n";
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
	std::cout << "Image and associated structs destroyed\n";
}