#include "../include/Context.h"
#include "../include/Errors.h"
#include <iostream>
#define STB_IMAGE_IMPLEMENTATION
#include <../stb_image/stb_image.h>
#include <../assimp/Importer.hpp>      // C++ importer interface
#include <../assimp/scene.h>           // Output data structure
#include <../assimp/postprocess.h>     // Post processing flags
//#define NV_GLSL_SHADER

vk::RenderPass Shadows::renderPass = nullptr;
bool Shadows::shadowCast = true;
uint32_t Shadows::imageSize = 2048;

vk::DescriptorSetLayout Mesh::descriptorSetLayout = nullptr;
vk::DescriptorSetLayout Model::descriptorSetLayout = nullptr;
vk::DescriptorSetLayout GUI::descriptorSetLayout = nullptr;
vk::DescriptorSetLayout SkyBox::descriptorSetLayout = nullptr;
vk::DescriptorSetLayout Shadows::descriptorSetLayout = nullptr;
vk::DescriptorSetLayout Terrain::descriptorSetLayout = nullptr;

bool Vertex::operator==(const Vertex& other) const
{
	return x == other.x && y == other.y && z == other.z
		&& nX == other.nX && nY == other.nY && nZ == other.nZ
		&& u == other.u && v == other.v
		&& tX == other.tX && tY == other.tY && tZ == other.tZ && tW == other.tW;
}

std::vector<vk::VertexInputBindingDescription> Vertex::getBindingDescriptionGeneral()
{
	std::vector<vk::VertexInputBindingDescription> vInputBindDesc(1);
	vInputBindDesc[0].binding = 0;
	vInputBindDesc[0].stride = sizeof(Vertex);
	vInputBindDesc[0].inputRate = vk::VertexInputRate::eVertex;

	return vInputBindDesc;
}

std::vector<vk::VertexInputBindingDescription> Vertex::getBindingDescriptionGUI()
{
	std::vector<vk::VertexInputBindingDescription> vInputBindDesc(1);
	vInputBindDesc[0].binding = 0;
	vInputBindDesc[0].stride = 5 * sizeof(float);
	vInputBindDesc[0].inputRate = vk::VertexInputRate::eVertex;

	return vInputBindDesc;
}

std::vector<vk::VertexInputBindingDescription> Vertex::getBindingDescriptionSkyBox()
{
	std::vector<vk::VertexInputBindingDescription> vInputBindDesc(1);
	vInputBindDesc[0].binding = 0;
	vInputBindDesc[0].stride = 4 * sizeof(float);
	vInputBindDesc[0].inputRate = vk::VertexInputRate::eVertex;

	return vInputBindDesc;
}

std::vector<vk::VertexInputAttributeDescription> Vertex::getAttributeDescriptionGeneral()
{
	std::vector<vk::VertexInputAttributeDescription> vInputAttrDesc(4);
	vInputAttrDesc[0] = vk::VertexInputAttributeDescription()
		.setBinding(0)										// index of the binding to get per-vertex data
		.setLocation(0)										// location directive of the input in the vertex shader
		.setFormat(vk::Format::eR32G32B32Sfloat)	//vec3
		.setOffset(0);
	vInputAttrDesc[1] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(1)
		.setFormat(vk::Format::eR32G32B32Sfloat)	//vec3
		.setOffset(3 * sizeof(float));
	vInputAttrDesc[2] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(2)
		.setFormat(vk::Format::eR32G32Sfloat)		//vec2
		.setOffset(6 * sizeof(float));
	vInputAttrDesc[3] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(3)
		.setFormat(vk::Format::eR32G32B32A32Sfloat) //vec4
		.setOffset(8 * sizeof(float));

	return vInputAttrDesc;
}

std::vector<vk::VertexInputAttributeDescription> Vertex::getAttributeDescriptionGUI()
{
	std::vector<vk::VertexInputAttributeDescription> vInputAttrDesc(2);
	vInputAttrDesc[0] = vk::VertexInputAttributeDescription()
		.setBinding(0)										// index of the binding to get per-vertex data
		.setLocation(0)										// location directive of the input in the vertex shader
		.setFormat(vk::Format::eR32G32B32Sfloat)	//vec3
		.setOffset(0);
	vInputAttrDesc[1] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(1)
		.setFormat(vk::Format::eR32G32Sfloat)		//vec2
		.setOffset(3 * sizeof(float));

	return vInputAttrDesc;
}

std::vector<vk::VertexInputAttributeDescription> Vertex::getAttributeDescriptionSkyBox()
{
	std::vector<vk::VertexInputAttributeDescription> vInputAttrDesc(1);
	vInputAttrDesc[0] = vk::VertexInputAttributeDescription()
		.setBinding(0)										// index of the binding to get per-vertex data
		.setLocation(0)										// location directive of the input in the vertex shader
		.setFormat(vk::Format::eR32G32B32A32Sfloat)	//vec4
		.setOffset(0);

	return vInputAttrDesc;
}

void Image::createImage(const Context* info, const uint32_t width, const uint32_t height, const vk::ImageTiling tiling, const vk::ImageUsageFlags usage, const vk::MemoryPropertyFlags properties, vk::SampleCountFlagBits samples)
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
	info->gpu.getImageFormatProperties(format, vk::ImageType::e2D, tiling, usage, vk::ImageCreateFlags(), &imageFormatProperties);

    check(info->device.createImage(&imageInfo, nullptr, &image));

    uint32_t memTypeIndex = UINT32_MAX; ///////////////////
    auto const memRequirements = info->device.getImageMemoryRequirements(image);
    auto const memProperties = info->gpu.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if (memRequirements.memoryTypeBits & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            memTypeIndex = i;
            break;
        }
    }
    if(memTypeIndex == UINT32_MAX)
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

    check(info->device.allocateMemory(&allocInfo, nullptr, &memory));
	check(info->device.bindImageMemory(image, memory, 0));

    if (image && memory)
        std::cout << "Image created\n";
    else
    {
        std::cout << "Error at image creation\n";
        exit(-1);
    }
}

void Image::createImageView(const Context* info, const vk::ImageAspectFlags aspectFlags)
{
	auto const viewInfo = vk::ImageViewCreateInfo()
		.setImage(image)
		.setViewType(viewType)
		.setFormat(format)
		.setSubresourceRange({ aspectFlags, 0, mipLevels, 0, arrayLayers });

	check(info->device.createImageView(&viewInfo, nullptr, &view));
    std::cout << "ImageView created\n";
}

void Image::transitionImageLayout(const Context* info, const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout)
{
	auto const allocInfo = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1)
		.setCommandPool(info->commandPool);

    vk::CommandBuffer commandBuffer;
	check(info->device.allocateCommandBuffers(&allocInfo, &commandBuffer));

	auto const beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	check(commandBuffer.begin(&beginInfo));

	auto barrier = vk::ImageMemoryBarrier()
		.setOldLayout(oldLayout)
		.setNewLayout(newLayout)
		.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
		.setImage(image);

    vk::PipelineStageFlags srcStage;
    vk::PipelineStageFlags dstStage;

    // ubresource aspectMask
    if (newLayout == vk::ImageLayout::eDepthStencilAttachmentOptimal) {
		barrier.setSubresourceRange({ vk::ImageAspectFlagBits::eDepth, 0, mipLevels, 0, arrayLayers });
        if (format == vk::Format::eD32SfloatS8Uint || format == vk::Format::eD24UnormS8Uint) {
            barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
        }
    }else barrier.setSubresourceRange({ vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, arrayLayers });

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

	check(commandBuffer.end());

	auto const submitInfo = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&commandBuffer);
	check(info->graphicsQueue.submit(1, &submitInfo, nullptr));

	check(info->graphicsQueue.waitIdle());
}

void Image::copyBufferToImage(const Context* info, const vk::Buffer buffer, const int x, const int y, const int width, const int height, const uint32_t baseLayer)
{
	auto const allocInfo = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1)
		.setCommandPool(info->commandPool);

    vk::CommandBuffer commandBuffer;
	check(info->device.allocateCommandBuffers(&allocInfo, &commandBuffer));

	auto const beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	check(commandBuffer.begin(&beginInfo));

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

	check(commandBuffer.end());

	auto const submitInfo = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&commandBuffer);
	check(info->graphicsQueue.submit(1, &submitInfo, nullptr));

	check(info->graphicsQueue.waitIdle());
}

void Image::generateMipMaps(const Context* info, const int32_t texWidth, const int32_t texHeight)
{
	auto const allocInfo = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1)
		.setCommandPool(info->commandPool);

	vk::CommandBuffer commandBuffer;
	check(info->device.allocateCommandBuffers(&allocInfo, &commandBuffer));

	auto const beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	check(commandBuffer.begin(&beginInfo));

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

	check(commandBuffer.end());

	auto const submitInfo = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&commandBuffer);
	check(info->graphicsQueue.submit(1, &submitInfo, nullptr));

	check(info->graphicsQueue.waitIdle());
}

void Image::createSampler(const Context* info)
{
	auto const samplerInfo = vk::SamplerCreateInfo()
		.setMagFilter(vk::Filter::eLinear)
		.setMinFilter(vk::Filter::eLinear)
		.setMipmapMode(vk::SamplerMipmapMode::eLinear)
		.setMinLod(0.f)
		.setMaxLod(static_cast<float>(mipLevels))
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
	check(info->device.createSampler(&samplerInfo, nullptr, &sampler));
}

void Buffer::createBuffer(const Context* info, const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags properties)
{
    this->size = size;
    //create buffer (GPU buffer)
	auto const bufferInfo = vk::BufferCreateInfo()
		.setSize(size)
		.setUsage(usage)
		.setSharingMode(vk::SharingMode::eExclusive); // used by graphics queue only
	check(info->device.createBuffer(&bufferInfo, nullptr, &buffer));

    uint32_t memTypeIndex = UINT32_MAX; ///////////////////
    auto const memRequirements = info->device.getBufferMemoryRequirements(buffer);
    auto const memProperties = info->gpu.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if (memRequirements.memoryTypeBits & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            memTypeIndex = i;
            break;
        }
    }
    if(memTypeIndex == UINT32_MAX)
    {
        std::cout << "No suitable memory type found\n";
        exit(-1);
    }

    if (this->size < memRequirements.size)
        this->size = memRequirements.size;
    //allocate memory of buffer
	auto const allocInfo = vk::MemoryAllocateInfo()
		.setAllocationSize(this->size)
		.setMemoryTypeIndex(memTypeIndex);
	check(info->device.allocateMemory(&allocInfo, nullptr, &memory));

    //binding memory with buffer
	check(info->device.bindBufferMemory(buffer, memory, 0));
    std::cout << "Buffer and memory created and binded\n";
}

void Buffer::copyBuffer(const Context* info, const vk::Buffer srcBuffer, const vk::DeviceSize size)
{
    vk::CommandBuffer copyCmd;

	auto const cbai = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandPool(info->commandPool)
		.setCommandBufferCount(1);
	check(info->device.allocateCommandBuffers(&cbai, &copyCmd));

    auto const cbbi = vk::CommandBufferBeginInfo();
	check(copyCmd.begin(&cbbi));

    vk::BufferCopy bufferCopy{};
    bufferCopy.size = size;

    copyCmd.copyBuffer(srcBuffer, buffer, 1, &bufferCopy);

	check(copyCmd.end());

	auto const si = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&copyCmd);
	check(info->graphicsQueue.submit(1, &si, nullptr));

	check(info->graphicsQueue.waitIdle());

    info->device.freeCommandBuffers(info->commandPool, 1, &copyCmd);
}

void Image::destroy(const Context* info)
{
	if (view)
		info->device.destroyImageView(view);
	if (image)
		info->device.destroyImage(image);
	if (memory)
		info->device.freeMemory(memory);
	if (sampler)
		info->device.destroySampler(sampler);
	view = nullptr;
	image = nullptr;
	memory = nullptr;
	sampler = nullptr;
	std::cout << "Image and associated structs destroyed\n";
}

void Swapchain::destroy(const vk::Device& device)
{
	for (auto &image : images) {
		device.destroyImageView(image.view);
		image.view = nullptr;
	}
	if (swapchain) {
		device.destroySwapchainKHR(swapchain);
		swapchain = nullptr;
		std::cout << "Swapchain destroyed\n";
	}
}

void Buffer::destroy(const Context* info)
{
	if (buffer)
		info->device.destroyBuffer(buffer);
	if (memory)
		info->device.freeMemory(memory);
	buffer = nullptr;
	memory = nullptr;
	std::cout << "Buffer and memory destroyed\n";
}

void Pipeline::destroy(const Context* info)
{
	if (this->info.layout) {
		info->device.destroyPipelineLayout(this->info.layout);
		this->info.layout = nullptr;
		std::cout << "Pipeline Layout destroyed\n";
	}

	if (pipeline) {
		info->device.destroyPipeline(pipeline);
		pipeline = nullptr;
		std::cout << "Pipeline destroyed\n";
	}
}

void Mesh::destroy(const Context* info)
{
	texture.destroy(info);
	normalsTexture.destroy(info);
	specularTexture.destroy(info);
	alphaTexture.destroy(info);
	vertices.clear();
	indices.clear();
}

void Model::destroy(const Context* info)
{
	for (auto& mesh : meshes) {
		mesh.vertices.clear();
		mesh.indices.clear();
	}

	for (auto& texture : uniqueTextures)
		texture.second.destroy(info);

	vertexBuffer.destroy(info);
	indexBuffer.destroy(info);
	uniformBuffer.destroy(info);
	std::cout << "Model and associated structs destroyed\n";
}

void Object::destroy(Context * info)
{
	texture.destroy(info);
	vertexBuffer.destroy(info);
	uniformBuffer.destroy(info);
	vertices.clear();
	std::cout << "GUI and associated structs destroyed\n";
}

void Shadows::destroy(const Context * info)
{
	texture.destroy(info);
	info->device.destroyFramebuffer(frameBuffer);
	uniformBuffer.destroy(info);
}

vk::DescriptorSetLayout Mesh::getDescriptorSetLayout(const Context * info)
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for mesh texture
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(1) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(2) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(3) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		check(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

void Mesh::loadTexture(TextureType type, const std::string path, const Context* info)
{
	// Texture Load
	int texWidth, texHeight, texChannels;
	//stbi_set_flip_vertically_on_load(true);
	stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	vk::DeviceSize imageSize = texWidth * texHeight * 4;

	if (!pixels) {
		std::cout << "Can not load texture\n";
		exit(-19);
	}

	Buffer staging;
	staging.createBuffer(info, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	void* data;
	info->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	info->device.unmapMemory(staging.memory);

	stbi_image_free(pixels);

	Image *tex = nullptr;
	switch (type)
	{
	case Mesh::DiffuseMap:
		tex = &texture;
		break;
	case Mesh::NormalMap:
		tex = &normalsTexture;
		break;
	case Mesh::SpecularMap:
		tex = &specularTexture;
		break;
	case Mesh::AlphaMap:
		tex = &alphaTexture;
		break;
	default:
		break;
	}

	tex->format = vk::Format::eR8G8B8A8Unorm;
	tex->mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
	tex->createImage(info, texWidth, texHeight, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	tex->transitionImageLayout(info, vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	tex->copyBufferToImage(info, staging.buffer, 0, 0, texWidth, texHeight);
	tex->generateMipMaps(info, texWidth, texHeight);
	tex->createImageView(info, vk::ImageAspectFlagBits::eColor);
	tex->createSampler(info);

	staging.destroy(info);
}

Model Model::loadModel(const std::string path, const std::string modelName, const Context* info)
{
	Model _model;
	_model.name = modelName;

	// Materials, Vertices and Indices load
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path + modelName,
		//aiProcess_MakeLeftHanded |
		aiProcess_FlipUVs |
		aiProcess_JoinIdenticalVertices |
		aiProcess_Triangulate |
		aiProcess_CalcTangentSpace |
		aiProcess_ImproveCacheLocality |
		aiProcess_OptimizeMeshes |
		aiProcess_OptimizeGraph
	);
	if (!scene) exit(-100);
	for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
		Mesh myMesh;
		const aiMesh* mesh = scene->mMeshes[i];
		const aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];

		aiColor3D aiAmbient(0.f, 0.f, 0.f);
		material->Get(AI_MATKEY_COLOR_AMBIENT, aiAmbient);
		myMesh.colorEffects.ambient = { aiAmbient.r, aiAmbient.g, aiAmbient.b, 0.f };

		aiColor3D aiDiffuse(1.f, 1.f, 1.f);
		material->Get(AI_MATKEY_COLOR_DIFFUSE, aiDiffuse);
		float aiOpacity = 1.f;
		material->Get(AI_MATKEY_OPACITY, aiOpacity);
		myMesh.colorEffects.diffuse = { aiDiffuse.r, aiDiffuse.g, aiDiffuse.b, aiOpacity };

		aiColor3D aiSpecular(0.f, 0.f, 0.f);
		material->Get(AI_MATKEY_COLOR_SPECULAR, aiSpecular);
		myMesh.colorEffects.specular = { aiSpecular.r, aiSpecular.g, aiSpecular.b, 100.f };

		aiString aitexPath;
		material->GetTexture(aiTextureType_DIFFUSE, 0, &aitexPath);
		std::string texPath = aitexPath.C_Str();
		if (texPath != "")	texPath = path + texPath;
		else				texPath = "objects/default.png";
		if (_model.uniqueTextures.find(texPath) != _model.uniqueTextures.end()) {
			myMesh.texture = _model.uniqueTextures[texPath];
		}
		else {
			myMesh.loadTexture(Mesh::DiffuseMap, texPath, info);
			_model.uniqueTextures[texPath] = myMesh.texture;
		}

		aiString aiNormTexPath;
		material->GetTexture(aiTextureType_HEIGHT, 0, &aiNormTexPath);
		std::string normTexPath = aiNormTexPath.C_Str();
		if (normTexPath != "")	
			normTexPath = path + normTexPath;
		else					normTexPath = "objects/defaultNormalMap.png";
		if (_model.uniqueTextures.find(normTexPath) != _model.uniqueTextures.end()) {
			myMesh.normalsTexture = _model.uniqueTextures[normTexPath];
		}
		else {
			myMesh.loadTexture(Mesh::NormalMap, normTexPath, info);
			_model.uniqueTextures[normTexPath] = myMesh.normalsTexture;
		}

		aiString aiSpecTexPath;
		material->GetTexture(aiTextureType_SPECULAR, 0, &aiSpecTexPath);
		std::string specTexPath = aiSpecTexPath.C_Str();
		if (specTexPath != "")	specTexPath = path + specTexPath;
		else					specTexPath = "objects/defaultSpecularMap.png";
		if (_model.uniqueTextures.find(specTexPath) != _model.uniqueTextures.end()) {
			myMesh.specularTexture = _model.uniqueTextures[specTexPath];
		}
		else {
			myMesh.loadTexture(Mesh::SpecularMap, specTexPath, info);
			_model.uniqueTextures[specTexPath] = myMesh.specularTexture;
		}

		aiString aiAlphaTexPath;
		material->GetTexture(aiTextureType_OPACITY, 0, &aiAlphaTexPath);
		std::string aplhaTexPath = aiAlphaTexPath.C_Str();
		if (aplhaTexPath != "")	aplhaTexPath = path + aplhaTexPath;
		else					aplhaTexPath = "objects/default.png";
		if (_model.uniqueTextures.find(aplhaTexPath) != _model.uniqueTextures.end()) {
			myMesh.alphaTexture = _model.uniqueTextures[aplhaTexPath];
		}
		else {
			myMesh.loadTexture(Mesh::AlphaMap, aplhaTexPath, info);
			_model.uniqueTextures[aplhaTexPath] = myMesh.alphaTexture;
		}

		for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
			const aiVector3D* pos = mesh->HasPositions() ? &(mesh->mVertices[j]) : &(aiVector3D(0.f, 0.f, 0.f));
			const aiVector3D* norm = mesh->HasNormals() ? &(mesh->mNormals[j]) : &(aiVector3D(0.f, 0.f, 0.f)); // -2.f to test if it has normals in shader
			const aiVector3D* uv = mesh->HasTextureCoords(0) ? &(mesh->mTextureCoords[0][j]) : &(aiVector3D(0.f, 0.f, 0.f));
			const aiVector3D* tangent = mesh->HasTangentsAndBitangents() ? &(mesh->mTangents[j]) : &(aiVector3D(0.f, 0.f, 0.f)); // -2.f to test if it has tangents in shader

			Vertex v{
				pos->x, pos->y, pos->z,
				norm->x, norm->y, norm->z,
				uv->x, uv->y,
				tangent->x, tangent->y, tangent->z, 1.0f // passing tangents here
			};
			myMesh.vertices.push_back(v);
		}
		for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
			const aiFace& Face = mesh->mFaces[i];
			assert(Face.mNumIndices == 3);
			myMesh.indices.push_back(Face.mIndices[0]);
			myMesh.indices.push_back(Face.mIndices[1]);
			myMesh.indices.push_back(Face.mIndices[2]);
		}
		_model.meshes.push_back(myMesh);

		_model.numberOfVertices += mesh->mNumVertices;
		_model.numberOfIndices += mesh->mNumFaces * 3;
	}

	_model.createVertexBuffer(info);
	_model.createIndexBuffer(info);
	_model.createUniformBuffers(info);
	_model.createDescriptorSets(info);

	// resizing the model to always be at a certain magnitude
	float scale = 2.0f / _model.getBoundingSphere().w;
	_model.matrix = glm::scale(_model.matrix, glm::vec3(scale, scale, scale));

	return _model;
}

vk::DescriptorSetLayout Model::getDescriptorSetLayout(const Context * info)
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for model mvp matrix
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eVertex)); // which pipeline shader stages can access


		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		check(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

specificGraphicsPipelineCreateInfo Model::getPipelineSpecifications(const Context * info)
{
	// General Pipeline
	specificGraphicsPipelineCreateInfo generalSpecific;
#ifdef NV_GLSL_SHADER
	generalSpecific.shaders = { "shaders/General/shader.vert", "shaders/General/shader.frag" };
#else
	generalSpecific.shaders = { "shaders/General/vert.spv", "shaders/General/frag.spv" };
#endif
	generalSpecific.renderPass = info->renderPass;
	generalSpecific.viewportSize = { info->surface.capabilities.currentExtent.width, info->surface.capabilities.currentExtent.height };
	generalSpecific.descriptorSetLayouts = { Model::getDescriptorSetLayout(info), Mesh::getDescriptorSetLayout(info), Shadows::getDescriptorSetLayout(info) };
	generalSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	generalSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	generalSpecific.cull = vk::CullModeFlagBits::eBack;
	generalSpecific.face = vk::FrontFace::eCounterClockwise;
	generalSpecific.pushConstantRange = vk::PushConstantRange()
		.setStageFlags(vk::ShaderStageFlagBits::eFragment)
		.setSize(info->gpuProperties.limits.maxPushConstantsSize);

	return generalSpecific;
}

// position x, y, z and radius w
glm::vec4 Model::getBoundingSphere()
{
	if (initialBoundingSphereRadius <= 0) {
		for (auto &mesh : meshes) {
			for (auto& vertex : mesh.vertices) {
				float dis = glm::length(glm::vec3(vertex.x, vertex.y, vertex.z));
				if (dis > initialBoundingSphereRadius)
					initialBoundingSphereRadius = dis;
			}
		}
	}

	glm::vec3 scale;
	glm::quat rotation;
	glm::vec3 translation;
	glm::vec3 skew;
	glm::vec4 perspective;
	glm::decompose(matrix, scale, rotation, translation, skew, perspective);

	glm::vec4 _boundingSphere;
	_boundingSphere = matrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0);
	_boundingSphere.w = scale.x * initialBoundingSphereRadius; // supposes the scale was uniform for all x, y, z

	return _boundingSphere;
}

void Model::createVertexBuffer(const Context* info)
{
	std::vector<Vertex> vertices{};
	for (auto& mesh : meshes) {
		for (auto& vertex : mesh.vertices)
			vertices.push_back(vertex);
	}

	vertexBuffer.createBuffer(info, sizeof(Vertex)*vertices.size(), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Staging buffer
	Buffer staging;
	staging.createBuffer(info, sizeof(Vertex)*vertices.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	check(info->device.mapMemory(staging.memory, 0, staging.size, vk::MemoryMapFlags(), &staging.data));
	memcpy(staging.data, vertices.data(), sizeof(Vertex)*vertices.size());
	info->device.unmapMemory(staging.memory);

	vertexBuffer.copyBuffer(info, staging.buffer, staging.size);
	staging.destroy(info);
}

void Model::createIndexBuffer(const Context* info)
{
	std::vector<uint32_t> indices{};
	for (auto& mesh : meshes) {
		for (auto& index : mesh.indices)
			indices.push_back(index);
	}
	indexBuffer.createBuffer(info, sizeof(uint32_t)*indices.size(), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Staging buffer
	Buffer staging;
	staging.createBuffer(info, sizeof(uint32_t)*indices.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	check(info->device.mapMemory(staging.memory, 0, staging.size, vk::MemoryMapFlags(), &staging.data));
	memcpy(staging.data, indices.data(), sizeof(uint32_t)*indices.size());
	info->device.unmapMemory(staging.memory);

	indexBuffer.copyBuffer(info, staging.buffer, staging.size);
	staging.destroy(info);
}

void Model::createUniformBuffers(const Context* info)
{
	// since the uniform buffers are unique for each model, they are not bigger than 256 in size
	uniformBuffer.createBuffer(info, 256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	check(info->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size, vk::MemoryMapFlags(), &uniformBuffer.data));
}

void Model::createDescriptorSets(const Context* info)
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(info->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&Model::descriptorSetLayout);
	check(info->device.allocateDescriptorSets(&allocateInfo, &descriptorSet));

	// Model MVP
	auto const mvpWriteSet = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)								// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)	        // DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorBufferInfo* pBufferInfo;
			.setBuffer(uniformBuffer.buffer)							// Buffer buffer;
			.setOffset(0)													// DeviceSize offset;
			.setRange(uniformBuffer.size));							// DeviceSize range;

	info->device.updateDescriptorSets(1, &mvpWriteSet, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";

	for (auto& mesh : meshes) {
		auto const allocateInfo = vk::DescriptorSetAllocateInfo()
			.setDescriptorPool(info->descriptorPool)
			.setDescriptorSetCount(1)
			.setPSetLayouts(&Mesh::descriptorSetLayout);
		check(info->device.allocateDescriptorSets(&allocateInfo, &mesh.descriptorSet));

		// Texture
		vk::WriteDescriptorSet textureWriteSets[4];

		textureWriteSets[0] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(0)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.texture.sampler)										// Sampler sampler;
				.setImageView(mesh.texture.view)								// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[1] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(1)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.normalsTexture.sampler)						// Sampler sampler;
				.setImageView(mesh.normalsTexture.view)							// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[2] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(2)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.specularTexture.sampler)						// Sampler sampler;
				.setImageView(mesh.specularTexture.view)						// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		textureWriteSets[3] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(3)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.alphaTexture.sampler)							// Sampler sampler;
				.setImageView(mesh.alphaTexture.view)							// ImageView imageView;
				.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

		info->device.updateDescriptorSets(4, textureWriteSets, 0, nullptr);
		std::cout << "DescriptorSet allocated and updated\n";
	}
}

vk::DescriptorSetLayout GUI::getDescriptorSetLayout(const Context* info)
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for gui texture
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access


		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		check(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

specificGraphicsPipelineCreateInfo GUI::getPipelineSpecifications(const Context * info)
{
	// GUI Pipeline
	specificGraphicsPipelineCreateInfo GUISpecific;
#ifdef NV_GLSL_SHADER
	GUISpecific.shaders = { "shaders/GUI/shaderGUI.vert", "shaders/GUI/shaderGUI.frag" };
#else
	GUISpecific.shaders = { "shaders/GUI/vert.spv", "shaders/GUI/frag.spv" };
#endif
	GUISpecific.renderPass = info->renderPass;
	GUISpecific.viewportSize = { info->surface.capabilities.currentExtent.width, info->surface.capabilities.currentExtent.height };
	GUISpecific.descriptorSetLayouts = { GUI::getDescriptorSetLayout(info) };
	GUISpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGUI();
	GUISpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGUI();
	GUISpecific.cull = vk::CullModeFlagBits::eBack;
	GUISpecific.face = vk::FrontFace::eCounterClockwise;
	GUISpecific.pushConstantRange = vk::PushConstantRange();

	return GUISpecific;
}

GUI GUI::loadGUI(const std::string textureName, const Context * info, bool show)
{
	GUI _gui;
	//					 counter clock wise
	// x, y coords orientation		// u, v coords orientation
	//			|  /|				// (0,0)-------------> u
	//			| /  +z				//	   |
	//			|/					//	   |
	//  --------|-------->			//	   |
	//		   /|		+x			//	   |
	//		  /	|					//	   |
	//	     /  |/ +y				//	   |/ v


	_gui.vertices = {
		-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
		-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
		 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
		 1.0f,  1.0f, 0.0f, 1.0f, 1.0f
	};
	_gui.loadTexture(textureName, info);
	_gui.createVertexBuffer(info);
	_gui.createDescriptorSet(GUI::descriptorSetLayout, info);
	_gui.enabled = show;

	return _gui;
}

void GUI::createDescriptorSet(vk::DescriptorSetLayout & descriptorSetLayout, const Context * info)
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(info->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	check(info->device.allocateDescriptorSets(&allocateInfo, &descriptorSet)); // why the handle of the vk::Image is changing with 2 dSets allocation????


	vk::WriteDescriptorSet textureWriteSets[1];
																		// texture sampler
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(texture.sampler)									// Sampler sampler;
			.setImageView(texture.view)										// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;
	info->device.updateDescriptorSets(1, textureWriteSets, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}

vk::DescriptorSetLayout SkyBox::getDescriptorSetLayout(const Context * info)
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for model mvp matrix
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eVertex)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(1) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		check(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

specificGraphicsPipelineCreateInfo SkyBox::getPipelineSpecifications(const Context * info)
{
	// SkyBox Pipeline
	specificGraphicsPipelineCreateInfo skyBoxSpecific;
#ifdef NV_GLSL_SHADER
	skyBoxSpecific.shaders = { "shaders/SkyBox/shaderSkyBox.vert", "shaders/SkyBox/shaderSkyBox.frag" };
#else
	skyBoxSpecific.shaders = { "shaders/SkyBox/vert.spv", "shaders/SkyBox/frag.spv" };
#endif
	skyBoxSpecific.renderPass = info->renderPass;
	skyBoxSpecific.viewportSize = { info->surface.capabilities.currentExtent.width, info->surface.capabilities.currentExtent.height };
	skyBoxSpecific.descriptorSetLayouts = { SkyBox::getDescriptorSetLayout(info) };
	skyBoxSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionSkyBox();
	skyBoxSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionSkyBox();
	skyBoxSpecific.cull = vk::CullModeFlagBits::eBack;
	skyBoxSpecific.face = vk::FrontFace::eCounterClockwise;
	skyBoxSpecific.pushConstantRange = vk::PushConstantRange();

	return skyBoxSpecific;
}

SkyBox SkyBox::loadSkyBox(const std::vector<std::string>& textureNames, uint32_t imageSideSize, const Context * info, bool show)
{
	SkyBox _skyBox;

	float SIZE = static_cast<float>(imageSideSize);
	_skyBox.vertices = {
		-SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,
							 
		-SIZE, -SIZE,  SIZE, 0.0f,
		-SIZE, -SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,
		-SIZE,  SIZE,  SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,
							 
		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
							 
		-SIZE, -SIZE,  SIZE, 0.0f,
		-SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE, -SIZE,  SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,
							 
		-SIZE,  SIZE, -SIZE, 0.0f,
		 SIZE,  SIZE, -SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		 SIZE,  SIZE,  SIZE, 0.0f,
		-SIZE,  SIZE,  SIZE, 0.0f,
		-SIZE,  SIZE, -SIZE, 0.0f,
							 
		-SIZE, -SIZE, -SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		 SIZE, -SIZE, -SIZE, 0.0f,
		-SIZE, -SIZE,  SIZE, 0.0f,
		 SIZE, -SIZE,  SIZE, 0.0f
	};
	_skyBox.loadTextures(textureNames, imageSideSize, info);
	_skyBox.createVertexBuffer(info);
	_skyBox.createUniformBuffer(info);
	_skyBox.createDescriptorSet(SkyBox::descriptorSetLayout, info);
	_skyBox.enabled = show;

	return _skyBox;
}

// images must be squared and the image size must be the real else the assertion will fail
void SkyBox::loadTextures(const std::vector<std::string>& paths, uint32_t imageSideSize, const Context * info)
{
	assert(paths.size() == 6);

	texture.arrayLayers = 6;
	texture.format = vk::Format::eR8G8B8A8Unorm;
	texture.imageCreateFlags = vk::ImageCreateFlagBits::eCubeCompatible;
	texture.createImage(info, imageSideSize, imageSideSize, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

	texture.transitionImageLayout(info, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
	for (uint32_t i = 0; i < texture.arrayLayers; ++i) {
		// Texture Load
		int texWidth, texHeight, texChannels;
		//stbi_set_flip_vertically_on_load(true);
		stbi_uc* pixels = stbi_load(paths[i].c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
		assert(imageSideSize == texWidth && imageSideSize == texHeight);

		vk::DeviceSize imageSize = texWidth * texHeight * 4;
		if (!pixels) {
			std::cout << "Can not load texture\n";
			exit(-19);
		}
		Buffer staging;
		staging.createBuffer(info, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		void* data;
		info->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &data);
		memcpy(data, pixels, static_cast<size_t>(imageSize));
		info->device.unmapMemory(staging.memory);
		stbi_image_free(pixels);
		
		texture.copyBufferToImage(info, staging.buffer, 0, 0, texWidth, texHeight, i);
		staging.destroy(info);
	}
	texture.transitionImageLayout(info, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

	texture.viewType = vk::ImageViewType::eCube;
	texture.createImageView(info, vk::ImageAspectFlagBits::eColor);

	texture.addressMode = vk::SamplerAddressMode::eClampToEdge;
	texture.createSampler(info);
}

void Object::createVertexBuffer(const Context * info)
{
	vertexBuffer.createBuffer(info, sizeof(float)*vertices.size(), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	// Staging buffer
	Buffer staging;
	staging.createBuffer(info, sizeof(float)*vertices.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	check(info->device.mapMemory(staging.memory, 0, staging.size, vk::MemoryMapFlags(), &staging.data));
	memcpy(staging.data, vertices.data(), sizeof(float)*vertices.size());
	info->device.unmapMemory(staging.memory);

	vertexBuffer.copyBuffer(info, staging.buffer, staging.size);
	staging.destroy(info);
}

void Object::createUniformBuffer(const Context * info)
{
	uniformBuffer.createBuffer(info, 256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	check(info->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size, vk::MemoryMapFlags(), &uniformBuffer.data));
}

void Object::loadTexture(const std::string path, const Context * info)
{
	// Texture Load
	int texWidth, texHeight, texChannels;
	//stbi_set_flip_vertically_on_load(true);
	stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	vk::DeviceSize imageSize = texWidth * texHeight * 4;

	if (!pixels) {
		std::cout << "Can not load texture\n";
		exit(-19);
	}

	Buffer staging;
	staging.createBuffer(info, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	void* data;
	info->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	info->device.unmapMemory(staging.memory);

	stbi_image_free(pixels);

	texture.format = vk::Format::eR8G8B8A8Unorm;
	texture.mipLevels = 1;
	texture.createImage(info, texWidth, texHeight, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	texture.transitionImageLayout(info, vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	texture.copyBufferToImage(info, staging.buffer, 0, 0, texWidth, texHeight);
	texture.transitionImageLayout(info, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
	texture.createImageView(info, vk::ImageAspectFlagBits::eColor);
	texture.createSampler(info);

	info->device.destroyBuffer(staging.buffer);
	info->device.freeMemory(staging.memory);
}

void Object::createDescriptorSet(vk::DescriptorSetLayout& descriptorSetLayout, const Context * info)
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(info->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	check(info->device.allocateDescriptorSets(&allocateInfo, &descriptorSet)); // why the handle of the vk::Image is changing with 2 dSets allocation????


	vk::WriteDescriptorSet textureWriteSets[2];
	// MVP
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)	        // DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorBufferInfo* pBufferInfo;
			.setBuffer(uniformBuffer.buffer)							// Buffer buffer;
			.setOffset(0)													// DeviceSize offset;
			.setRange(uniformBuffer.size));									// DeviceSize range;
	// texture sampler
	textureWriteSets[1] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(1)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(texture.sampler)									// Sampler sampler;
			.setImageView(texture.view)										// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;
	info->device.updateDescriptorSets(2, textureWriteSets, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}

void Shadows::createDescriptorSet(vk::DescriptorSetLayout & descriptorSetLayout, const Context * info)
{
	auto allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(info->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	check(info->device.allocateDescriptorSets(&allocateInfo, &descriptorSet)); // why the handle of the vk::Image is changing with 2 dSets allocation????

	vk::WriteDescriptorSet textureWriteSets[2];	
	// MVP
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)							// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)	        // DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorBufferInfo* pBufferInfo;
			.setBuffer(uniformBuffer.buffer)							// Buffer buffer;
			.setOffset(0)													// DeviceSize offset;
			.setRange(uniformBuffer.size));									// DeviceSize range;
	// sampler
	textureWriteSets[1] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(1)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
		.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
			.setSampler(texture.sampler)									// Sampler sampler;
			.setImageView(texture.view)										// ImageView imageView;
			.setImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal));		// ImageLayout imageLayout;

	info->device.updateDescriptorSets(2, textureWriteSets, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";
}

vk::DescriptorSetLayout Shadows::getDescriptorSetLayout(const Context * info)
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for model mvp matrix
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eVertex)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(1) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		check(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

vk::RenderPass Shadows::getRenderPass(const Context * info)
{
	if (!renderPass) {
		auto attachment = vk::AttachmentDescription()
			.setFormat(info->depth.format)
			.setSamples(vk::SampleCountFlagBits::e1)
			.setLoadOp(vk::AttachmentLoadOp::eClear)
			.setStoreOp(vk::AttachmentStoreOp::eStore)
			.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
			.setStencilStoreOp(vk::AttachmentStoreOp::eStore)
			.setInitialLayout(vk::ImageLayout::eUndefined)
			.setFinalLayout(vk::ImageLayout::eDepthAttachmentStencilReadOnlyOptimal);

		auto const depthAttachmentRef = vk::AttachmentReference()
			.setAttachment(0)
			.setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

		auto const subpassDesc = vk::SubpassDescription() // subpass desc (there can be multiple subpasses)
			.setPDepthStencilAttachment(&depthAttachmentRef);

		std::vector<vk::SubpassDependency> spDependencies{
			vk::SubpassDependency{
			VK_SUBPASS_EXTERNAL,
			0,
			vk::PipelineStageFlagBits::eBottomOfPipe,
			vk::PipelineStageFlagBits::eLateFragmentTests,
			vk::AccessFlagBits::eShaderRead,
			vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::DependencyFlagBits::eByRegion
		},
			vk::SubpassDependency{
			0,
			VK_SUBPASS_EXTERNAL,
			vk::PipelineStageFlagBits::eLateFragmentTests,
			vk::PipelineStageFlagBits::eFragmentShader,
			vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
			vk::AccessFlagBits::eShaderRead,
			vk::DependencyFlagBits::eByRegion
		}
		};

		auto const rpci = vk::RenderPassCreateInfo()
			.setAttachmentCount(1)
			.setPAttachments(&attachment)
			.setSubpassCount(1)
			.setPSubpasses(&subpassDesc)
			.setDependencyCount((uint32_t)spDependencies.size())
			.setPDependencies(spDependencies.data());

		check(info->device.createRenderPass(&rpci, nullptr, &renderPass));
		std::cout << "RenderPass created\n";
	}
	return renderPass;
}

specificGraphicsPipelineCreateInfo Shadows::getPipelineSpecifications(const Context* info)
{
	// Shadows Pipeline
	specificGraphicsPipelineCreateInfo shadowsSpecific;

#ifdef NV_GLSL_SHADER
	shadowsSpecific.shaders = { "shaders/Shadows/shaderShadows.vert" };
#else
	shadowsSpecific.shaders = { "shaders/Shadows/vert.spv" };
#endif
	shadowsSpecific.renderPass = Shadows::getRenderPass(info);
	shadowsSpecific.viewportSize = { Shadows::imageSize, Shadows::imageSize };
	shadowsSpecific.useBlendState = false;
	shadowsSpecific.sampleCount = vk::SampleCountFlagBits::e1;
	shadowsSpecific.descriptorSetLayouts = { Shadows::getDescriptorSetLayout() };
	shadowsSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	shadowsSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	shadowsSpecific.cull = vk::CullModeFlagBits::eBack;
	shadowsSpecific.face = vk::FrontFace::eCounterClockwise;
	shadowsSpecific.pushConstantRange = vk::PushConstantRange();

	return shadowsSpecific;
}

void Shadows::createFrameBuffer(const Context * info)
{
	texture.format = info->depth.format;
	texture.addressMode = vk::SamplerAddressMode::eClampToEdge;
	texture.initialLayout = vk::ImageLayout::eUndefined;
	texture.createImage(info, imageSize, imageSize, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);

	texture.createImageView(info, vk::ImageAspectFlagBits::eDepth);

	texture.addressMode = vk::SamplerAddressMode::eClampToEdge;
	texture.maxAnisotropy = 1.f;
	texture.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	texture.samplerCompareEnable = VK_TRUE;
	texture.createSampler(info);

	auto const fbci = vk::FramebufferCreateInfo()
		.setRenderPass(Shadows::getRenderPass(info))
		.setAttachmentCount(1)
		.setPAttachments(&texture.view)
		.setWidth(imageSize)
		.setHeight(imageSize)
		.setLayers(1);
	check(info->device.createFramebuffer(&fbci, nullptr, &frameBuffer));
	std::cout << "Framebuffer created\n";

}

void Shadows::createUniformBuffer(const Context * info)
{
	uniformBuffer.createBuffer(info, 256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	check(info->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size, vk::MemoryMapFlags(), &uniformBuffer.data));
}

vk::DescriptorSetLayout Terrain::getDescriptorSetLayout(const Context * info)
{
	if (!descriptorSetLayout) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};

		// binding for model mvp matrix
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eVertex)); // which pipeline shader stages can access

		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(1) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
			.setPImmutableSamplers(nullptr)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access

		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		check(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

specificGraphicsPipelineCreateInfo Terrain::getPipelineSpecifications(const Context * info)
{
	// Terrain Pipeline
	specificGraphicsPipelineCreateInfo terrainSpecific;
#ifdef NV_GLSL_SHADER
	terrainSpecific.shaders = { "shaders/Terrain/shaderTerrain.vert", "shaders/Terrain/shaderTerrain.frag" };
#else
	terrainSpecific.shaders = { "shaders/Terrain/vert.spv", "shaders/Terrain/frag.spv" };
#endif
	terrainSpecific.renderPass = info->renderPass;
	terrainSpecific.viewportSize = { info->surface.capabilities.currentExtent.width, info->surface.capabilities.currentExtent.height };
	terrainSpecific.descriptorSetLayouts = { Terrain::getDescriptorSetLayout(info) };
	terrainSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	terrainSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	terrainSpecific.cull = vk::CullModeFlagBits::eBack;
	terrainSpecific.face = vk::FrontFace::eCounterClockwise;
	terrainSpecific.pushConstantRange = vk::PushConstantRange();

	return terrainSpecific;
}

Terrain Terrain::generateTerrain(const std::string path, const Context* info)
{
	auto size = 100.f;
	Terrain _terrain;

	_terrain.vertices = {
		// x,    y,    z,    nX,    nY,   nZ,   u,    v,    tX,   tY,   tZ,   tW
		-size, -0.1f, -size, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		-size, -0.1f,  size, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		 size, -0.1f, -size, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		 size, -0.1f, -size, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		-size, -0.1f,  size, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
		 size, -0.1f,  size, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f
	};
	std::string texPath = path == "" ? "objects/defaultTerrainMap.png" : path;
	_terrain.loadTexture(texPath, info);
	_terrain.createVertexBuffer(info);
	_terrain.createUniformBuffer(info);
	_terrain.createDescriptorSet(Terrain::descriptorSetLayout, info);

	return _terrain;
}

void Terrain::loadTexture(const std::string path, const Context* info)
{
	// Texture Load
	int texWidth, texHeight, texChannels;
	//stbi_set_flip_vertically_on_load(true);
	stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	vk::DeviceSize imageSize = texWidth * texHeight * 4;

	if (!pixels) {
		std::cout << "Can not load texture\n";
		exit(-19);
	}

	Buffer staging;
	staging.createBuffer(info, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	void* data;
	info->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	info->device.unmapMemory(staging.memory);

	stbi_image_free(pixels);

	texture.format = vk::Format::eR8G8B8A8Unorm;
	texture.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
	texture.createImage(info, texWidth, texHeight, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	texture.transitionImageLayout(info, vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	texture.copyBufferToImage(info, staging.buffer, 0, 0, texWidth, texHeight);
	texture.generateMipMaps(info, texWidth, texHeight);
	texture.createImageView(info, vk::ImageAspectFlagBits::eColor);
	texture.createSampler(info);

	staging.destroy(info);
}

Camera::Camera()
{
	worldUp = glm::vec3(0.0f, -1.0f, 0.0f);
	yaw = -90.f;
	pitch = 0.0f;
	speed = 0.35f;
	rotationSpeed = 0.05f;
	front = glm::normalize(
		glm::vec3(
			-cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
			sin(glm::radians(pitch)),
			sin(glm::radians(yaw)) * cos(glm::radians(pitch))
		)
	);
	right = glm::normalize(glm::cross(front, worldUp));
	up = glm::normalize(glm::cross(right, front));
}

void Camera::move(RelativeDirection direction, float deltaTime, bool combineDirections)
{
	float velocity = speed * deltaTime;
	if (combineDirections) velocity *= 0.707f;
	if (direction == RelativeDirection::FORWARD)
		position += front * velocity;
	if (direction == RelativeDirection::BACKWARD)
		position -= front * velocity;
	if (direction == RelativeDirection::LEFT)
		position += right * velocity;
	if (direction == RelativeDirection::RIGHT)
		position -= right * velocity;
}

void Camera::rotate(float xoffset, float yoffset)
{
	xoffset *= rotationSpeed;
	yoffset *= rotationSpeed;

	yaw += xoffset;
	pitch += yoffset;

	if (pitch > 89.0f)
		pitch = 89.0f;
	if (pitch < -89.0f)
		pitch = -89.0f;

	// update the vectors
	front = glm::normalize(
		glm::vec3(
			-cos(glm::radians(yaw)) * cos(glm::radians(pitch)),
			sin(glm::radians(pitch)),
			sin(glm::radians(yaw)) * cos(glm::radians(pitch))
		)
	);
	right = glm::normalize(glm::cross(front, worldUp));
	up = glm::normalize(glm::cross(right, front));
}

glm::mat4 Camera::getPerspective()
{
	return glm::perspective(glm::radians(FOV), aspect, nearPlane, farPlane);
}

glm::mat4 Camera::getView()
{
	return glm::lookAt(position, position + front, up);
}

