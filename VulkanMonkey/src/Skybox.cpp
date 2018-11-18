#include "../include/Skybox.h"
#include "../include/Errors.h"
#include <iostream>

vk::DescriptorSetLayout	SkyBox::descriptorSetLayout = nullptr;

vk::DescriptorSetLayout SkyBox::getDescriptorSetLayout(vk::Device device)
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

void SkyBox::loadSkyBox(const std::vector<std::string>& textureNames, uint32_t imageSideSize, vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, bool show)
{
	float SIZE = static_cast<float>(imageSideSize);
	vertices = {
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
	loadTextures(device, gpu, commandPool, graphicsQueue, textureNames, imageSideSize);
	createVertexBuffer(device, gpu, commandPool, graphicsQueue);
	createUniformBuffer(device, gpu, 2 * sizeof(vm::mat4));
	createDescriptorSet(device, descriptorPool, SkyBox::descriptorSetLayout);
	render = show;
}

void SkyBox::draw(Pipeline& pipeline, const vk::CommandBuffer & cmd)
{
	if (render)
	{
		vk::DeviceSize offset{ 0 };
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.pipeinfo.layout, 0, 1, &descriptorSet, 0, nullptr);
		cmd.bindVertexBuffers(0, 1, &vertexBuffer.buffer, &offset);
		cmd.draw(static_cast<uint32_t>(vertices.size() * 0.25f), 1, 0, 0);
	}
}

// images must be squared and the image size must be the real else the assertion will fail
void SkyBox::loadTextures(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, const std::vector<std::string>& paths, uint32_t imageSideSize)
{
	assert(paths.size() == 6);

	texture.arrayLayers = 6;
	texture.format = vk::Format::eR8G8B8A8Unorm;
	texture.imageCreateFlags = vk::ImageCreateFlagBits::eCubeCompatible;
	texture.createImage(device, gpu, imageSideSize, imageSideSize, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);

	texture.transitionImageLayout(device, commandPool, graphicsQueue, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
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
		staging.createBuffer(device, gpu, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		void* data;
		device.mapMemory(staging.memory, vk::DeviceSize(), imageSize, vk::MemoryMapFlags(), &data);
		memcpy(data, pixels, static_cast<size_t>(imageSize));
		device.unmapMemory(staging.memory);
		stbi_image_free(pixels);

		texture.copyBufferToImage(device, commandPool, graphicsQueue, staging.buffer, 0, 0, texWidth, texHeight, i);
		staging.destroy(device);
	}
	texture.transitionImageLayout(device, commandPool, graphicsQueue, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);

	texture.viewType = vk::ImageViewType::eCube;
	texture.createImageView(device, vk::ImageAspectFlagBits::eColor);

	texture.addressMode = vk::SamplerAddressMode::eClampToEdge;
	texture.createSampler(device);
}