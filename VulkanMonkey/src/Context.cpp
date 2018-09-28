#include "../include/Context.h"
#include "../include/Errors.h"
#include <iostream>
#include <future>
#define STB_IMAGE_IMPLEMENTATION
#include "../include/stb_image/stb_image.h"
#include "../include/assimp/Importer.hpp"      // C++ importer interface
#include "../include/assimp/scene.h"           // Output data structure
#include "../include/assimp/postprocess.h"     // Post processing flags
#include "../include/assimp/DefaultLogger.hpp"

vk::RenderPass Shadows::renderPass = nullptr;
bool Shadows::shadowCast = true;
uint32_t Shadows::imageSize = 4096;

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
	vInputBindDesc[0].stride = sizeof(ImDrawVert); //5 * sizeof(float);
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
	std::vector<vk::VertexInputAttributeDescription> vInputAttrDesc(3);
	vInputAttrDesc[0] = vk::VertexInputAttributeDescription()
		.setBinding(0)										// index of the binding to get per-vertex data
		.setLocation(0)										// location directive of the input in the vertex shader
		.setFormat(vk::Format::eR32G32Sfloat)		//vec2
		.setOffset(IM_OFFSETOF(ImDrawVert, pos));//(0);
	vInputAttrDesc[1] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(1)
		.setFormat(vk::Format::eR32G32Sfloat)		//vec2
		.setOffset(IM_OFFSETOF(ImDrawVert, uv));//(3 * sizeof(float));
	vInputAttrDesc[2] = vk::VertexInputAttributeDescription()
		.setBinding(0)
		.setLocation(2)
		.setFormat(vk::Format::eR8G8B8A8Unorm)		//unsigned integer
		.setOffset(IM_OFFSETOF(ImDrawVert, col));

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

    VkCheck(info->device.createImage(&imageInfo, nullptr, &image));

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

    VkCheck(info->device.allocateMemory(&allocInfo, nullptr, &memory));
	VkCheck(info->device.bindImageMemory(image, memory, 0));

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

	VkCheck(info->device.createImageView(&viewInfo, nullptr, &view));
    std::cout << "ImageView created\n";
}

void Image::transitionImageLayout(const Context* info, const vk::ImageLayout oldLayout, const vk::ImageLayout newLayout)
{
	auto const allocInfo = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1)
		.setCommandPool(info->commandPool);

    vk::CommandBuffer commandBuffer;
	VkCheck(info->device.allocateCommandBuffers(&allocInfo, &commandBuffer));

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

	VkCheck(commandBuffer.end());

	auto const submitInfo = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&commandBuffer);
	VkCheck(info->graphicsQueue.submit(1, &submitInfo, nullptr));

	VkCheck(info->graphicsQueue.waitIdle());
}

void Image::copyBufferToImage(const Context* info, const vk::Buffer buffer, const int x, const int y, const int width, const int height, const uint32_t baseLayer)
{
	auto const allocInfo = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1)
		.setCommandPool(info->commandPool);

    vk::CommandBuffer commandBuffer;
	VkCheck(info->device.allocateCommandBuffers(&allocInfo, &commandBuffer));

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
	VkCheck(info->graphicsQueue.submit(1, &submitInfo, nullptr));

	VkCheck(info->graphicsQueue.waitIdle());
}

void Image::generateMipMaps(const Context* info, const int32_t texWidth, const int32_t texHeight)
{
	auto const allocInfo = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(1)
		.setCommandPool(info->commandPool);

	vk::CommandBuffer commandBuffer;
	VkCheck(info->device.allocateCommandBuffers(&allocInfo, &commandBuffer));

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
	VkCheck(info->graphicsQueue.submit(1, &submitInfo, nullptr));

	VkCheck(info->graphicsQueue.waitIdle());
}

void Image::createSampler(const Context* info)
{
	auto const samplerInfo = vk::SamplerCreateInfo()
		.setMagFilter(vk::Filter::eLinear)
		.setMinFilter(vk::Filter::eLinear)
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
	VkCheck(info->device.createSampler(&samplerInfo, nullptr, &sampler));
}

void Buffer::createBuffer(const Context* info, const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags properties)
{
    this->size = size;
    //create buffer (GPU buffer)
	auto const bufferInfo = vk::BufferCreateInfo()
		.setSize(size)
		.setUsage(usage)
		.setSharingMode(vk::SharingMode::eExclusive); // used by graphics queue only
	VkCheck(info->device.createBuffer(&bufferInfo, nullptr, &buffer));

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
	VkCheck(info->device.allocateMemory(&allocInfo, nullptr, &memory));

    //binding memory with buffer
	VkCheck(info->device.bindBufferMemory(buffer, memory, 0));
    std::cout << "Buffer and memory created and binded\n";
}

void Buffer::copyBuffer(const Context* info, const vk::Buffer srcBuffer, const vk::DeviceSize size)
{
    vk::CommandBuffer copyCmd;

	auto const cbai = vk::CommandBufferAllocateInfo()
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandPool(info->commandPool)
		.setCommandBufferCount(1);
	VkCheck(info->device.allocateCommandBuffers(&cbai, &copyCmd));

    auto const cbbi = vk::CommandBufferBeginInfo();
	VkCheck(copyCmd.begin(&cbbi));

    vk::BufferCopy bufferCopy{};
    bufferCopy.size = size;

    copyCmd.copyBuffer(srcBuffer, buffer, 1, &bufferCopy);

	VkCheck(copyCmd.end());

	auto const si = vk::SubmitInfo()
		.setCommandBufferCount(1)
		.setPCommandBuffers(&copyCmd);
	VkCheck(info->graphicsQueue.submit(1, &si, nullptr));

	VkCheck(info->graphicsQueue.waitIdle());

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
	vertices.shrink_to_fit();
	indices.clear();
	indices.shrink_to_fit();
}

void Model::destroy(const Context* info)
{
	for (auto& mesh : meshes) {
		mesh.vertices.clear();
		mesh.vertices.shrink_to_fit();
		mesh.indices.clear();
		mesh.indices.shrink_to_fit();
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
	indexBuffer.destroy(info);
	uniformBuffer.destroy(info);
	vertices.clear();
	vertices.shrink_to_fit();
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
		VkCheck(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
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
		std::cout << "Can not load texture: " << path << "\n";
		exit(-19);
	}

	Buffer staging;
	staging.createBuffer(info, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	void* data;
	info->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	info->device.unmapMemory(staging.memory);

	stbi_image_free(pixels);

	Image* tex = nullptr;
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

	tex->name = path;
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

glm::mat4 aiMatrix4x4ToGlmMat4(const aiMatrix4x4 m)
{
	glm::mat4 _m;
	for (uint32_t i = 0; i < 4; i++) {
		for (uint32_t j = 0; j < 4; j++)
			_m[i][j] = m[j][i];
	}
	return _m;
}

void getAllNodes(aiNode* node, std::vector<aiNode*>& allNodes) {
	for (uint32_t i = 0; i < node->mNumChildren; i++)
		getAllNodes(node->mChildren[i], allNodes);
	if(node) allNodes.push_back(node);
}

Model Model::loadModel(const std::string path, const std::string modelName, const Context* info, bool show)
{
	Model _model;

	// Materials, Vertices and Indices load
	Assimp::Logger::LogSeverity severity = Assimp::Logger::VERBOSE;
	// Create a logger instance for Console Output
	Assimp::DefaultLogger::create("", severity, aiDefaultLogStream_STDOUT);

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path + modelName,
		//aiProcess_MakeLeftHanded |
		aiProcess_FlipUVs |
		aiProcess_JoinIdenticalVertices |
		aiProcess_Triangulate |
		aiProcess_CalcTangentSpace //|
		//aiProcess_ImproveCacheLocality |
		//aiProcess_OptimizeMeshes |
		//aiProcess_OptimizeGraph
	);
	if (!scene) exit(-100);

	std::vector<aiNode*> allNodes{};
	getAllNodes(scene->mRootNode, allNodes);

	std::vector<Mesh> f_meshes{};
	for (unsigned int n = 0; n < allNodes.size(); n++) {
		const aiNode* node = allNodes[n];
		for (unsigned int i = 0; i < node->mNumMeshes; i++) {
			Mesh myMesh;

			const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
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
			if (normTexPath != "")	normTexPath = path + normTexPath;
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
				glm::vec4 p = aiMatrix4x4ToGlmMat4(node->mTransformation) * glm::vec4(pos->x, pos->y, pos->z, 1);
				Vertex v{
					p.x, p.y, p.z,
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
			f_meshes.push_back(myMesh);
		}
	}
	for (auto &m : f_meshes) {
		m.vertexOffset = _model.numberOfVertices;
		m.indexOffset = _model.numberOfIndices;

		m.calculateBoundingSphere();
		_model.meshes.push_back(m);
		_model.numberOfVertices += static_cast<uint32_t>(_model.meshes.back().vertices.size());
		_model.numberOfIndices += static_cast<uint32_t>(_model.meshes.back().indices.size());
	}

	_model.createVertexBuffer(info);
	_model.createIndexBuffer(info);
	_model.createUniformBuffers(info);
	_model.createDescriptorSets(info);
	_model.name = modelName;
	_model.render = show;

	// resizing the model to always be at a certain magnitude
	float scale = 2.0f / _model.getBoundingSphere().w;
	_model.matrix = glm::scale(_model.matrix, glm::vec3(scale, scale, scale));

	return _model;
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

	glm::vec4 _boundingSphere = matrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0);
	_boundingSphere.w = scale.x * initialBoundingSphereRadius; // supposes the scale was uniform for all x, y, z

	return _boundingSphere;
}

void Mesh::calculateBoundingSphere()
{
	float maxX = 0, maxY = 0, maxZ = 0, minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
	for (auto& vertex : vertices) {
		if (vertex.x > maxX) maxX = vertex.x;
		if (vertex.y > maxY) maxY = vertex.y;
		if (vertex.z > maxZ) maxZ = vertex.z;
		if (vertex.x < minX) minX = vertex.x;
		if (vertex.y < minY) minY = vertex.y;
		if (vertex.z < minZ) minZ = vertex.z;
	}
	glm::vec3 center = (glm::vec3(maxX, maxY, maxZ) + glm::vec3(minX, minY, minZ)) * .5f;
	float sphereRadius = glm::length(glm::vec3(maxX, maxY, maxZ) - center);
	boundingSphere = glm::vec4(center, sphereRadius);
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
		VkCheck(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

vk::DescriptorSetLayout getDescriptorSetLayoutLights(Context * info)
{
	// binding for model mvp matrix
	if (!info->descriptorSetLayoutLights) {
		std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBinding{};
		descriptorSetLayoutBinding.push_back(vk::DescriptorSetLayoutBinding()
			.setBinding(0) // binding number in shader stages
			.setDescriptorCount(1) // number of descriptors contained
			.setDescriptorType(vk::DescriptorType::eUniformBuffer)
			.setStageFlags(vk::ShaderStageFlagBits::eFragment)); // which pipeline shader stages can access
		auto const createInfo = vk::DescriptorSetLayoutCreateInfo()
			.setBindingCount((uint32_t)descriptorSetLayoutBinding.size())
			.setPBindings(descriptorSetLayoutBinding.data());
		VkCheck(info->device.createDescriptorSetLayout(&createInfo, nullptr, &info->descriptorSetLayoutLights));
		std::cout << "Descriptor Set Layout created\n";
	}
	return info->descriptorSetLayoutLights;
}

specificGraphicsPipelineCreateInfo Model::getPipelineSpecifications(Context * info)
{
	// General Pipeline
	specificGraphicsPipelineCreateInfo generalSpecific;
	generalSpecific.shaders = { "shaders/General/vert.spv", "shaders/General/frag.spv" };
	generalSpecific.renderPass = info->renderPass;
	generalSpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	generalSpecific.descriptorSetLayouts = { Shadows::getDescriptorSetLayout(info), Mesh::getDescriptorSetLayout(info), Model::getDescriptorSetLayout(info), getDescriptorSetLayoutLights(info) };
	generalSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	generalSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	generalSpecific.cull = vk::CullModeFlagBits::eBack;
	generalSpecific.face = vk::FrontFace::eCounterClockwise;
	generalSpecific.pushConstantRange = vk::PushConstantRange();
		//.setStageFlags(vk::ShaderStageFlagBits::eFragment)
		//.setSize(info->gpuProperties.limits.maxPushConstantsSize);

	return generalSpecific;
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

	VkCheck(info->device.mapMemory(staging.memory, 0, staging.size, vk::MemoryMapFlags(), &staging.data));
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

	VkCheck(info->device.mapMemory(staging.memory, 0, staging.size, vk::MemoryMapFlags(), &staging.data));
	memcpy(staging.data, indices.data(), sizeof(uint32_t)*indices.size());
	info->device.unmapMemory(staging.memory);

	indexBuffer.copyBuffer(info, staging.buffer, staging.size);
	staging.destroy(info);
}

void Model::createUniformBuffers(const Context* info)
{
	// since the uniform buffers are unique for each model, they are not bigger than 256 in size
	uniformBuffer.createBuffer(info, 256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(info->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size, vk::MemoryMapFlags(), &uniformBuffer.data));
}

void Model::createDescriptorSets(const Context* info)
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(info->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&Model::descriptorSetLayout);
	VkCheck(info->device.allocateDescriptorSets(&allocateInfo, &descriptorSet));

	// Model MVP
	auto const mvpWriteSet = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)								// DescriptorSet dstSet;
		.setDstBinding(0)										// uint32_t dstBinding;
		.setDstArrayElement(0)									// uint32_t dstArrayElement;
		.setDescriptorCount(1)									// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBuffer)	// DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()				// const DescriptorBufferInfo* pBufferInfo;
			.setBuffer(uniformBuffer.buffer)						// Buffer buffer;
			.setOffset(0)											// DeviceSize offset;
			.setRange(uniformBuffer.size));							// DeviceSize range;

	info->device.updateDescriptorSets(1, &mvpWriteSet, 0, nullptr);
	std::cout << "DescriptorSet allocated and updated\n";

	for (auto& mesh : meshes) {
		auto const allocateInfo = vk::DescriptorSetAllocateInfo()
			.setDescriptorPool(info->descriptorPool)
			.setDescriptorSetCount(1)
			.setPSetLayouts(&Mesh::descriptorSetLayout);
		VkCheck(info->device.allocateDescriptorSets(&allocateInfo, &mesh.descriptorSet));

		// Texture
		vk::WriteDescriptorSet textureWriteSets[4];

		textureWriteSets[0] = vk::WriteDescriptorSet()
			.setDstSet(mesh.descriptorSet)									// DescriptorSet dstSet;
			.setDstBinding(0)												// uint32_t dstBinding;
			.setDstArrayElement(0)											// uint32_t dstArrayElement;
			.setDescriptorCount(1)											// uint32_t descriptorCount;
			.setDescriptorType(vk::DescriptorType::eCombinedImageSampler)	// DescriptorType descriptorType;
			.setPImageInfo(&vk::DescriptorImageInfo()						// const DescriptorImageInfo* pImageInfo;
				.setSampler(mesh.texture.sampler)								// Sampler sampler;
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
		VkCheck(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

specificGraphicsPipelineCreateInfo GUI::getPipelineSpecifications(const Context * info)
{
	// GUI Pipeline
	specificGraphicsPipelineCreateInfo GUISpecific;
	GUISpecific.shaders = { "shaders/GUI/vert.spv", "shaders/GUI/frag.spv" };
	GUISpecific.renderPass = info->renderPass;
	GUISpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	GUISpecific.descriptorSetLayouts = { GUI::getDescriptorSetLayout(info) };
	GUISpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGUI();
	GUISpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGUI();
	GUISpecific.cull = vk::CullModeFlagBits::eNone;
	GUISpecific.face = vk::FrontFace::eCounterClockwise;
	GUISpecific.pushConstantRange = vk::PushConstantRange{ vk::ShaderStageFlagBits::eVertex, 0, sizeof(float) * 4 };
	GUISpecific.sampleCount = vk::SampleCountFlagBits::e4;
	GUISpecific.dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	GUISpecific.dynamicStateInfo = {
		vk::PipelineDynamicStateCreateFlags(),
		static_cast<uint32_t>(GUISpecific.dynamicStates.size()),
		GUISpecific.dynamicStates.data()
	};;

	return GUISpecific;
}

SDL_Window*  GUI::g_Window = NULL;
Uint64       GUI::g_Time = 0;
bool         GUI::g_MousePressed[3] = { false, false, false };
SDL_Cursor*  GUI::g_MouseCursors[ImGuiMouseCursor_COUNT] = { 0 };
char*        GUI::g_ClipboardTextData = NULL;

const char* GUI::ImGui_ImplSDL2_GetClipboardText(void*)
{
	if (g_ClipboardTextData)
		SDL_free(g_ClipboardTextData);
	g_ClipboardTextData = SDL_GetClipboardText();
	return g_ClipboardTextData;
}

void GUI::ImGui_ImplSDL2_SetClipboardText(void*, const char* text)
{
	SDL_SetClipboardText(text);
}

void GUI::initImGui(const Context* context)
{
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	g_Window = context->window;

	// Setup back-end capabilities flags
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;       // We can honor GetMouseCursor() values (optional)
	io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;        // We can honor io.WantSetMousePos requests (optional, rarely used)

	// Keyboard mapping. ImGui will use those indices to peek into the io.KeysDown[] array.
	io.KeyMap[ImGuiKey_Tab] = SDL_SCANCODE_TAB;
	io.KeyMap[ImGuiKey_LeftArrow] = SDL_SCANCODE_LEFT;
	io.KeyMap[ImGuiKey_RightArrow] = SDL_SCANCODE_RIGHT;
	io.KeyMap[ImGuiKey_UpArrow] = SDL_SCANCODE_UP;
	io.KeyMap[ImGuiKey_DownArrow] = SDL_SCANCODE_DOWN;
	io.KeyMap[ImGuiKey_PageUp] = SDL_SCANCODE_PAGEUP;
	io.KeyMap[ImGuiKey_PageDown] = SDL_SCANCODE_PAGEDOWN;
	io.KeyMap[ImGuiKey_Home] = SDL_SCANCODE_HOME;
	io.KeyMap[ImGuiKey_End] = SDL_SCANCODE_END;
	io.KeyMap[ImGuiKey_Insert] = SDL_SCANCODE_INSERT;
	io.KeyMap[ImGuiKey_Delete] = SDL_SCANCODE_DELETE;
	io.KeyMap[ImGuiKey_Backspace] = SDL_SCANCODE_BACKSPACE;
	io.KeyMap[ImGuiKey_Space] = SDL_SCANCODE_SPACE;
	io.KeyMap[ImGuiKey_Enter] = SDL_SCANCODE_RETURN;
	io.KeyMap[ImGuiKey_Escape] = SDL_SCANCODE_ESCAPE;
	io.KeyMap[ImGuiKey_A] = SDL_SCANCODE_A;
	io.KeyMap[ImGuiKey_C] = SDL_SCANCODE_C;
	io.KeyMap[ImGuiKey_V] = SDL_SCANCODE_V;
	io.KeyMap[ImGuiKey_X] = SDL_SCANCODE_X;
	io.KeyMap[ImGuiKey_Y] = SDL_SCANCODE_Y;
	io.KeyMap[ImGuiKey_Z] = SDL_SCANCODE_Z;

	io.SetClipboardTextFn = ImGui_ImplSDL2_SetClipboardText;
	io.GetClipboardTextFn = ImGui_ImplSDL2_GetClipboardText;
	io.ClipboardUserData = NULL;

	g_MouseCursors[ImGuiMouseCursor_Arrow] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	g_MouseCursors[ImGuiMouseCursor_TextInput] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
	g_MouseCursors[ImGuiMouseCursor_ResizeAll] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
	g_MouseCursors[ImGuiMouseCursor_ResizeNS] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
	g_MouseCursors[ImGuiMouseCursor_ResizeEW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
	g_MouseCursors[ImGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
	g_MouseCursors[ImGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
	g_MouseCursors[ImGuiMouseCursor_Hand] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);

#ifdef _WIN32
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(context->window, &wmInfo);
	io.ImeWindowHandle = wmInfo.info.win.window;
#else
	(void)window;
#endif
	ImGui::StyleColorsDark();
	
	auto beginInfo = vk::CommandBufferBeginInfo()
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
		.setPInheritanceInfo(nullptr);
	VkCheck(context->dynamicCmdBuffer.begin(&beginInfo));

	// Create fonts texture
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	size_t upload_size = width * height * 4 * sizeof(char);

	// Create the Image:
	{
		texture.format = vk::Format::eR8G8B8A8Unorm;
		texture.mipLevels = 1;
		texture.arrayLayers = 1;
		texture.initialLayout = vk::ImageLayout::eUndefined;
		texture.createImage(context, width, height, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

		texture.viewType = vk::ImageViewType::e2D;
		texture.createImageView(context, vk::ImageAspectFlagBits::eColor);

		texture.addressMode = vk::SamplerAddressMode::eRepeat;
		texture.maxAnisotropy = 1.f;
		texture.minLod = -1000.f;
		texture.maxLod = 1000.f;
		texture.createSampler(context);

		createDescriptorSet(getDescriptorSetLayout(context), context);
	}
	// Create the and Upload to Buffer:
	Buffer stagingBuffer;
	{
		stagingBuffer.createBuffer(context, upload_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible);
		void* map;
		VkCheck(context->device.mapMemory(stagingBuffer.memory, 0, upload_size, vk::MemoryMapFlags(), &map));
		memcpy(map, pixels, upload_size);
		vk::MappedMemoryRange range[1] = {};
		range[0].memory = stagingBuffer.memory;
		range[0].size = upload_size;
		VkCheck(context->device.flushMappedMemoryRanges(1, range));
		context->device.unmapMemory(stagingBuffer.memory);
	}

	// Copy to Image:
	{
		vk::ImageMemoryBarrier copy_barrier[1] = {};
		copy_barrier[0].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		copy_barrier[0].oldLayout = vk::ImageLayout::eUndefined;
		copy_barrier[0].newLayout = vk::ImageLayout::eTransferDstOptimal;
		copy_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copy_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		copy_barrier[0].image = texture.image;
		copy_barrier[0].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		copy_barrier[0].subresourceRange.levelCount = 1;
		copy_barrier[0].subresourceRange.layerCount = 1;
		context->dynamicCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, copy_barrier);

		vk::BufferImageCopy region = {};
		region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.width = width;
		region.imageExtent.height = height;
		region.imageExtent.depth = 1;
		context->dynamicCmdBuffer.copyBufferToImage(stagingBuffer.buffer, texture.image, vk::ImageLayout::eTransferDstOptimal, 1, &region);

		vk::ImageMemoryBarrier use_barrier[1] = {};
		use_barrier[0].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		use_barrier[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
		use_barrier[0].oldLayout = vk::ImageLayout::eTransferDstOptimal;
		use_barrier[0].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		use_barrier[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		use_barrier[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		use_barrier[0].image = texture.image;
		use_barrier[0].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		use_barrier[0].subresourceRange.levelCount = 1;
		use_barrier[0].subresourceRange.layerCount = 1;
		context->dynamicCmdBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags(), 0, nullptr, 0, nullptr, 1, use_barrier);
	}

	// Store our identifier
	io.Fonts->TexID = (ImTextureID)(intptr_t)(VkImage)texture.image;

	vk::SubmitInfo end_info = {};
	end_info.commandBufferCount = 1;
	end_info.pCommandBuffers = &context->dynamicCmdBuffer;
	VkCheck(context->dynamicCmdBuffer.end());
	context->graphicsQueue.submit(1, &end_info, nullptr);

	context->device.waitIdle();
	stagingBuffer.destroy(context);
}

void GUI::newFrame(const Context* info, SDL_Window* window)
{
	ImGuiIO& io = ImGui::GetIO();
	IM_ASSERT(io.Fonts->IsBuilt());     // Font atlas needs to be built, call renderer _NewFrame() function e.g. ImGui_ImplOpenGL3_NewFrame() 

	// Setup display size (every frame to accommodate for window resizing)
	int w, h;
	int display_w, display_h;
	SDL_GetWindowSize(window, &w, &h);
	SDL_GL_GetDrawableSize(window, &display_w, &display_h);
	io.DisplaySize = ImVec2((float)w, (float)h);
	io.DisplayFramebufferScale = ImVec2(w > 0 ? ((float)display_w / w) : 0, h > 0 ? ((float)display_h / h) : 0);

	// Setup time step (we don't use SDL_GetTicks() because it is using millisecond resolution)
	static Uint64 frequency = SDL_GetPerformanceFrequency();
	Uint64 current_time = SDL_GetPerformanceCounter();
	io.DeltaTime = g_Time > 0 ? (float)((double)(current_time - g_Time) / frequency) : (float)(1.0f / 60.0f);
	g_Time = current_time;

	// Set OS mouse position if requested (rarely used, only when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
	if (io.WantSetMousePos)
		SDL_WarpMouseInWindow(g_Window, (int)io.MousePos.x, (int)io.MousePos.y);
	else
		io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

	int mx, my;
	Uint32 mouse_buttons = SDL_GetMouseState(&mx, &my);
	io.MouseDown[0] = g_MousePressed[0] || (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;  // If a mouse press event came, always pass it as "mouse held this frame", so we don't miss click-release events that are shorter than 1 frame.
	io.MouseDown[1] = g_MousePressed[1] || (mouse_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
	io.MouseDown[2] = g_MousePressed[2] || (mouse_buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
	g_MousePressed[0] = g_MousePressed[1] = g_MousePressed[2] = false;

#if SDL_HAS_CAPTURE_MOUSE && !defined(__EMSCRIPTEN__)
	SDL_Window* focused_window = SDL_GetKeyboardFocus();
	if (g_Window == focused_window)
	{
		// SDL_GetMouseState() gives mouse position seemingly based on the last window entered/focused(?)
		// The creation of a new windows at runtime and SDL_CaptureMouse both seems to severely mess up with that, so we retrieve that position globally.
		int wx, wy;
		SDL_GetWindowPosition(focused_window, &wx, &wy);
		SDL_GetGlobalMouseState(&mx, &my);
		mx -= wx;
		my -= wy;
		io.MousePos = ImVec2((float)mx, (float)my);
	}

	// SDL_CaptureMouse() let the OS know e.g. that our imgui drag outside the SDL window boundaries shouldn't e.g. trigger the OS window resize cursor. 
	// The function is only supported from SDL 2.0.4 (released Jan 2016)
	bool any_mouse_button_down = ImGui::IsAnyMouseDown();
	SDL_CaptureMouse(any_mouse_button_down ? SDL_TRUE : SDL_FALSE);
#else
	if (SDL_GetWindowFlags(g_Window) & SDL_WINDOW_INPUT_FOCUS)
		io.MousePos = ImVec2((float)mx, (float)my);
#endif
	if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange)
		return;

	ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
	if (io.MouseDrawCursor || imgui_cursor == ImGuiMouseCursor_None)
	{
		// Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
		//SDL_ShowCursor(SDL_FALSE);
	}
	else
	{
		// Show OS mouse cursor
		SDL_SetCursor(g_MouseCursors[imgui_cursor] ? g_MouseCursors[imgui_cursor] : g_MouseCursors[ImGuiMouseCursor_Arrow]);
		//SDL_ShowCursor(SDL_TRUE);
	}

	ImGui::NewFrame();

	if (show_demo_window)
		ImGui::ShowDemoWindow(&show_demo_window);

	ImGui::Render();

	auto draw_data = ImGui::GetDrawData();
	if (draw_data->TotalVtxCount < 1)
		return;
	size_t vertex_size = draw_data->TotalVtxCount * sizeof(ImDrawVert);
	size_t index_size = draw_data->TotalIdxCount * sizeof(ImDrawIdx);
	if (!vertexBuffer.buffer || vertexBuffer.size < vertex_size)
		createVertexBuffer(info, vertex_size);
	if (!indexBuffer.buffer || indexBuffer.size < index_size)
		createIndexBuffer(info, index_size);

	// Upload Vertex and index Data:
	{
		ImDrawVert* vtx_dst = NULL;
		ImDrawIdx* idx_dst = NULL;
		VkCheck(info->device.mapMemory(vertexBuffer.memory, 0, vertex_size, vk::MemoryMapFlags(), (void**)(&vtx_dst)));
		VkCheck(info->device.mapMemory(indexBuffer.memory, 0, index_size, vk::MemoryMapFlags(), (void**)(&idx_dst)));
		for (int n = 0; n < draw_data->CmdListsCount; n++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[n];
			memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtx_dst += cmd_list->VtxBuffer.Size;
			idx_dst += cmd_list->IdxBuffer.Size;
		}
		vk::MappedMemoryRange range[2] = {};
		range[0].memory = vertexBuffer.memory;
		range[0].size = VK_WHOLE_SIZE;
		range[1].memory = indexBuffer.memory;
		range[1].size = VK_WHOLE_SIZE;
		VkCheck(info->device.flushMappedMemoryRanges(2, range));
		info->device.unmapMemory(vertexBuffer.memory);
		info->device.unmapMemory(indexBuffer.memory);
	}
}

GUI GUI::loadGUI(const std::string textureName, const Context * info, bool show)
{
	GUI _gui;

	if (textureName == "ImGuiDemo")
		_gui.show_demo_window = true;

	//					 counter clock wise
	// x, y, z coords orientation	// u, v coords orientation
	//			|  /|				// (0,0)-------------> u
	//			| /  +z				//	   |
	//			|/					//	   |
	//  --------|-------->			//	   |
	//		   /|		+x			//	   |
	//		  /	|					//	   |
	//	     /  |/ +y				//	   |/ v


	//_gui.vertices = {
	//	-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
	//	-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
	//	 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
	//	 1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
	//	-1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
	//	 1.0f,  1.0f, 0.0f, 1.0f, 1.0f
	//};
	//_gui.loadTexture(textureName, info);
	//_gui.createVertexBuffer(info, _gui.vertices.size());
	//_gui.createDescriptorSet(GUI::descriptorSetLayout, info);

	_gui.initImGui(info);
	_gui.render = show;

	return _gui;
}

void GUI::createVertexBuffer(const Context * info, size_t vertex_size)
{
	vertexBuffer.destroy(info);
	vertexBuffer.createBuffer(info, vertex_size, vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
}

void GUI::createIndexBuffer(const Context * info, size_t index_size)
{
	indexBuffer.destroy(info);
	indexBuffer.createBuffer(info, index_size, vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eHostVisible);
}

void GUI::createDescriptorSet(vk::DescriptorSetLayout & descriptorSetLayout, const Context * info)
{
	auto const allocateInfo = vk::DescriptorSetAllocateInfo()
		.setDescriptorPool(info->descriptorPool)
		.setDescriptorSetCount(1)
		.setPSetLayouts(&descriptorSetLayout);
	VkCheck(info->device.allocateDescriptorSets(&allocateInfo, &descriptorSet)); // why the handle of the vk::Image is changing with 2 dSets allocation????


	// texture sampler
	vk::WriteDescriptorSet textureWriteSets[1];
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
		VkCheck(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

specificGraphicsPipelineCreateInfo SkyBox::getPipelineSpecifications(const Context * info)
{
	// SkyBox Pipeline
	specificGraphicsPipelineCreateInfo skyBoxSpecific;
	skyBoxSpecific.shaders = { "shaders/SkyBox/vert.spv", "shaders/SkyBox/frag.spv" };
	skyBoxSpecific.renderPass = info->renderPass;
	skyBoxSpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
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
	_skyBox.render = show;

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
			std::cout << "Can not load texture: " << paths[i] << "\n";
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

	VkCheck(info->device.mapMemory(staging.memory, 0, staging.size, vk::MemoryMapFlags(), &staging.data));
	memcpy(staging.data, vertices.data(), sizeof(float)*vertices.size());
	info->device.unmapMemory(staging.memory);

	vertexBuffer.copyBuffer(info, staging.buffer, staging.size);
	staging.destroy(info);
}

void Object::createUniformBuffer(const Context * info)
{
	uniformBuffer.createBuffer(info, 256, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(info->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size, vk::MemoryMapFlags(), &uniformBuffer.data));
}

void Object::loadTexture(const std::string path, const Context * info)
{
	// Texture Load
	int texWidth, texHeight, texChannels;
	//stbi_set_flip_vertically_on_load(true);
	stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	vk::DeviceSize imageSize = texWidth * texHeight * 4;

	if (!pixels) {
		std::cout << "Can not load texture: " << path << "\n";
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
	VkCheck(info->device.allocateDescriptorSets(&allocateInfo, &descriptorSet)); // why the handle of the vk::Image is changing with 2 dSets allocation????


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
	VkCheck(info->device.allocateDescriptorSets(&allocateInfo, &descriptorSet)); // why the handle of the vk::Image is changing with 2 dSets allocation????

	vk::WriteDescriptorSet textureWriteSets[2];	
	// MVP
	textureWriteSets[0] = vk::WriteDescriptorSet()
		.setDstSet(descriptorSet)										// DescriptorSet dstSet;
		.setDstBinding(0)												// uint32_t dstBinding;
		.setDstArrayElement(0)											// uint32_t dstArrayElement;
		.setDescriptorCount(1)											// uint32_t descriptorCount;
		.setDescriptorType(vk::DescriptorType::eUniformBufferDynamic)	// DescriptorType descriptorType;
		.setPBufferInfo(&vk::DescriptorBufferInfo()						// const DescriptorBufferInfo* pBufferInfo;
			.setBuffer(uniformBuffer.buffer)							// Buffer buffer;
			.setOffset(0)													// DeviceSize offset;
			.setRange(sizeof(ShadowsUBO)));									// DeviceSize range;
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
			.setDescriptorType(vk::DescriptorType::eUniformBufferDynamic)
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
		VkCheck(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
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

		VkCheck(info->device.createRenderPass(&rpci, nullptr, &renderPass));
		std::cout << "RenderPass created\n";
	}
	return renderPass;
}

specificGraphicsPipelineCreateInfo Shadows::getPipelineSpecifications(const Context* info)
{
	// Shadows Pipeline
	specificGraphicsPipelineCreateInfo shadowsSpecific;
	shadowsSpecific.shaders = { "shaders/Shadows/vert.spv" };
	shadowsSpecific.renderPass = Shadows::getRenderPass(info);
	shadowsSpecific.viewportSize = { Shadows::imageSize, Shadows::imageSize };
	shadowsSpecific.useBlendState = false;
	shadowsSpecific.sampleCount = vk::SampleCountFlagBits::e1;
	shadowsSpecific.descriptorSetLayouts = { Shadows::getDescriptorSetLayout(info) };
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
	VkCheck(info->device.createFramebuffer(&fbci, nullptr, &frameBuffer));
	std::cout << "Framebuffer created\n";

}

void Shadows::createDynamicUniformBuffer(size_t num_of_objects, const Context * info)
{
	if (num_of_objects > 256) {
		std::cout << "256 objects for the Shadows Dynamic Uniform Buffer is the max for now\n";
		exit(-21);
	}
	size_t size = num_of_objects * 256;
	uniformBuffer.createBuffer(info, size, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	VkCheck(info->device.mapMemory(uniformBuffer.memory, 0, uniformBuffer.size, vk::MemoryMapFlags(), &uniformBuffer.data));
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
		VkCheck(info->device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

specificGraphicsPipelineCreateInfo Terrain::getPipelineSpecifications(const Context * info)
{
	// Terrain Pipeline
	specificGraphicsPipelineCreateInfo terrainSpecific;
	terrainSpecific.shaders = { "shaders/Terrain/vert.spv", "shaders/Terrain/frag.spv" };
	terrainSpecific.renderPass = info->renderPass;
	terrainSpecific.viewportSize = { info->surface.actualExtent.width, info->surface.actualExtent.height };
	terrainSpecific.descriptorSetLayouts = { Terrain::getDescriptorSetLayout(info) };
	terrainSpecific.vertexInputBindingDescriptions = Vertex::getBindingDescriptionGeneral();
	terrainSpecific.vertexInputAttributeDescriptions = Vertex::getAttributeDescriptionGeneral();
	terrainSpecific.cull = vk::CullModeFlagBits::eBack;
	terrainSpecific.face = vk::FrontFace::eCounterClockwise;
	terrainSpecific.pushConstantRange = vk::PushConstantRange();

	return terrainSpecific;
}

Terrain Terrain::generateTerrain(const std::string path, const Context* info, bool show)
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
	_terrain.render = show;

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
		std::cout << "Can not load texture: " << path << "\n";
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