#pragma once
#include "../VulkanContext/VulkanContext.h"
#include "../Object/Object.h"
#include "../Pipeline/Pipeline.h"
#include "../../include/Stbi.h"
#include "../Math/Math.h"

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