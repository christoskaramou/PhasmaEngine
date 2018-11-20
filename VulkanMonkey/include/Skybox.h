#pragma once
#include "Vulkan.h"
#include "Object.h"
#include "Pipeline.h"
#include "Stbi.h"
#include "Math.h"

namespace vm {
	struct SkyBox : Object
	{
		Pipeline pipeline;

		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		void loadSkyBox(const std::array<std::string, 6>& textureNames, uint32_t imageSideSize, vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, vk::DescriptorPool descriptorPool, bool show = true);
		void draw(Pipeline& pipeline, const vk::CommandBuffer & cmd);
		void loadTextures(vk::Device device, vk::PhysicalDevice gpu, vk::CommandPool commandPool, vk::Queue graphicsQueue, const std::array<std::string, 6>& paths, uint32_t imageSideSize);
		void destroy(vk::Device device);
	};
}