#pragma once
#include "Vulkan.h"
#include "Object.h"
#include "Pipeline.h"
#include "Stbi.h"
#include "Math.h"

struct Terrain : Object
{
	Pipeline pipelineTerrain;

	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
	void generateTerrain(const std::string path, vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, bool show = true);
	void draw(Pipeline& pipeline, const vk::CommandBuffer& cmd);
	void loadTexture(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, const std::string path);
};