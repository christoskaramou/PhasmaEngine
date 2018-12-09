#include "Terrain.h"
#include <iostream>

using namespace vm;

vk::DescriptorSetLayout Terrain::descriptorSetLayout = nullptr;

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
		descriptorSetLayout = device.createDescriptorSetLayout(createInfo);
	}
	return descriptorSetLayout;
}

void Terrain::generateTerrain(const std::string path, bool show)
{
	auto size = 100.f;

	vertices = {
		// x,    y,    z,     u,    v,   nX,    nY,   nZ,   tX,   tY,   tZ,   bX,   bY,   bZ,   r,    g,    b,    a
		-size, -0.1f, -size, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		-size, -0.1f,  size, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		 size, -0.1f, -size, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		 size, -0.1f, -size, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		-size, -0.1f,  size, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		 size, -0.1f,  size, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f
	};
	std::string texPath = path == "" ? "objects/defaultTerrainMap.png" : path;
	loadTexture(texPath);
	createVertexBuffer();
	render = show;
}

void Terrain::draw(const vk::CommandBuffer & cmd)
{
	if (render)
	{
		vk::DeviceSize offset = vk::DeviceSize();
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
		cmd.bindVertexBuffers(0, vertexBuffer.buffer, offset);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, descriptorSet, nullptr);
		cmd.draw(static_cast<uint32_t>(vertices.size() * 0.0833333333333333f), 1, 0, 0);
	}
}

void Terrain::loadTexture(const std::string path)
{
	// Texture Load
	int texWidth, texHeight, texChannels;
	//stbi_set_flip_vertically_on_load(true);
	stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	vk::DeviceSize imageSize = texWidth * texHeight * 4;

	if (!pixels) {
		exit(-19);
	}

	Buffer staging;
	staging.createBuffer(imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	void* data = vulkan->device.mapMemory(staging.memory, vk::DeviceSize(), imageSize);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	vulkan->device.unmapMemory(staging.memory);

	stbi_image_free(pixels);

	texture.format = vk::Format::eR8G8B8A8Unorm;
	texture.mipLevels = static_cast<uint32_t>(std::floor(std::log2(texWidth > texHeight ? texWidth : texHeight))) + 1;
	texture.createImage(texWidth, texHeight, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal);
	texture.transitionImageLayout(vk::ImageLayout::ePreinitialized, vk::ImageLayout::eTransferDstOptimal);
	texture.copyBufferToImage(staging.buffer, 0, 0, texWidth, texHeight);
	texture.generateMipMaps(texWidth, texHeight);
	texture.createImageView(vk::ImageAspectFlagBits::eColor);
	texture.createSampler();

	staging.destroy();
}

void Terrain::destroy()
{
	Object::destroy();
	pipeline.destroy();
	if (Terrain::descriptorSetLayout) {
		vulkan->device.destroyDescriptorSetLayout(Terrain::descriptorSetLayout);
		Terrain::descriptorSetLayout = nullptr;
	}
}
