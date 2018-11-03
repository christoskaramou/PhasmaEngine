#include "../include/Terrain.h"
#include "../include/Errors.h"
#include <iostream>

vk::DescriptorSetLayout	Terrain::descriptorSetLayout = nullptr;

vk::DescriptorSetLayout Terrain::getDescriptorSetLayout(vk::Device device)
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
		VkCheck(device.createDescriptorSetLayout(&createInfo, nullptr, &descriptorSetLayout));
		std::cout << "Descriptor Set Layout created\n";
	}
	return descriptorSetLayout;
}

Terrain Terrain::generateTerrain(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, const std::string path, bool show)
{
	auto size = 100.f;
	Terrain _terrain;

	_terrain.vertices = {
		// x,    y,    z,    nX,    nY,   nZ,   u,    v,    tX,   tY,   tZ,   tW    r,    g,    b,    a
		-size, -0.1f, -size, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		-size, -0.1f,  size, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		 size, -0.1f, -size, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		 size, -0.1f, -size, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		-size, -0.1f,  size, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		 size, -0.1f,  size, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
	};
	std::string texPath = path == "" ? "objects/defaultTerrainMap.png" : path;
	_terrain.loadTexture(device, gpu, commandPool, graphicsQueue, texPath);
	_terrain.createVertexBuffer(device, gpu, commandPool, graphicsQueue);
	_terrain.createUniformBuffer(device, gpu);
	_terrain.createDescriptorSet(device, descriptorPool, Terrain::descriptorSetLayout);
	_terrain.render = show;

	return _terrain;
}

void Terrain::draw(Pipeline& pipeline, const vk::CommandBuffer & cmd)
{
	if (render)
	{
		vk::DeviceSize offset = vk::DeviceSize();
		const vk::DescriptorSet descriptorSets[] = { descriptorSet };
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
		cmd.bindVertexBuffers(0, 1, &vertexBuffer.buffer, &offset);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, 1, descriptorSets, 0, nullptr);
		cmd.draw(static_cast<uint32_t>(vertices.size() * 0.0833333333333333f), 1, 0, 0);
	}
}

void Terrain::loadTexture(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, const std::string path)
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
	staging.createBuffer(device, gpu, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	void* data;
	device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	device.unmapMemory(staging.memory);

	stbi_image_free(pixels);

	texture.format = vk::Format::eR8G8B8A8Unorm;
	texture.mipLevels = static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
	texture.createImage(device, gpu, texWidth, texHeight, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	texture.transitionImageLayout(device, commandPool, graphicsQueue, vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	texture.copyBufferToImage(device, commandPool, graphicsQueue, staging.buffer, 0, 0, texWidth, texHeight);
	texture.generateMipMaps(device, commandPool, graphicsQueue, texWidth, texHeight);
	texture.createImageView(device, vk::ImageAspectFlagBits::eColor);
	texture.createSampler(device);

	staging.destroy(device);
}