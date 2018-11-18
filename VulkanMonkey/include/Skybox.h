#pragma once
#include "Vulkan.h"
#include "Object.h"
#include "Pipeline.h"
#include "Stbi.h"
#include "Math.h"

struct SkyBox : Object
{
	Pipeline pipelineSkyBox;

	static vk::DescriptorSetLayout descriptorSetLayout;
	static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
	void loadSkyBox(const std::vector<std::string>& textureNames, uint32_t imageSideSize, vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, bool show = true);
	void draw(Pipeline& pipeline, const vk::CommandBuffer & cmd);
	void loadTextures(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, const std::vector<std::string>& paths, uint32_t imageSideSize);
};