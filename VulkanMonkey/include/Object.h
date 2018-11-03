#pragma once
#include "Vulkan.h"
#include "Image.h"
#include "Buffer.h"
#include "Stbi.h"

struct Object
{
	virtual ~Object() = default;
	bool render = true, cull = false;
	vk::DescriptorSet descriptorSet;
	Image texture;
	std::vector<float> vertices{};
	Buffer vertexBuffer, indexBuffer, uniformBuffer;

	virtual void createVertexBuffer(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue);
	virtual void createUniformBuffer(vk::Device device, vk::PhysicalDevice gpu);
	virtual void loadTexture(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, const std::string path);
	virtual void createDescriptorSet(vk::Device device, vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout& descriptorSetLayout);
	virtual void destroy(vk::Device device);
};