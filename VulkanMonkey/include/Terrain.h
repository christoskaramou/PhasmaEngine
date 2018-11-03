#pragma once
#include "Vulkan.h"
#include "Object.h"
#include "Pipeline.h"
#include "Stbi.h"

struct Terrain : Object
{
	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
	static Terrain generateTerrain(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, const std::string path, bool show = true);
	void draw(Pipeline& pipeline, const vk::CommandBuffer& cmd);
	void loadTexture(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, const std::string path);
};