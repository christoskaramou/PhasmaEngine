#pragma once
#include "VulkanContext.h"
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
		void loadSkyBox(const std::array<std::string, 6>& textureNames, uint32_t imageSideSize, bool show = true);
		void draw(const vk::CommandBuffer & cmd);
		void loadTextures(const std::array<std::string, 6>& paths, uint32_t imageSideSize);
		void destroy();
	};
}