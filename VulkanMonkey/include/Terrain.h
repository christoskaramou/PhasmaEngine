#pragma once
#include "VulkanContext.h"
#include "Object.h"
#include "Pipeline.h"
#include "Stbi.h"
#include "Math.h"

namespace vm {
	struct Terrain : Object
	{
		Terrain(VulkanContext* vulkan);

		Pipeline pipeline = Pipeline(vulkan);

		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		void generateTerrain(const std::string path, bool show = true);
		void draw(const vk::CommandBuffer& cmd);
		void loadTexture(const std::string path);
		void destroy();
	};
}