#pragma once
#include "Vulkan.h"
#include "Object.h"
#include "Pipeline.h"
#include "Stbi.h"

struct SkyBox : Object
{
	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
	static SkyBox loadSkyBox(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, const std::vector<std::string>& textureNames, uint32_t imageSideSize, bool show = true);
	void draw(Pipeline& pipeline, const vk::CommandBuffer & cmd);
	void loadTextures(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, const std::vector<std::string>& paths, uint32_t imageSideSize);
};