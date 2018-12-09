#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Object/Object.h"
#include "../Pipeline/Pipeline.h"
#include "../Math/Math.h"
#include "../../include/Stbi.h"

namespace vm {
	struct Terrain : Object
	{
		Pipeline pipeline;

		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout(vk::Device device);
		void generateTerrain(const std::string path, bool show = true);
		void draw(const vk::CommandBuffer& cmd);
		void loadTexture(const std::string path);
		void destroy();
	};
}