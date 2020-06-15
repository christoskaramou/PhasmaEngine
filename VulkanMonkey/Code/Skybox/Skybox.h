#pragma once
#include "../Model/Object.h"
#include "../Pipeline/Pipeline.h"

namespace vm
{
	class SkyBox
	{
	public:
		Image texture;
		vk::DescriptorSet descriptorSet;
		static vk::DescriptorSetLayout descriptorSetLayout;
		static vk::DescriptorSetLayout getDescriptorSetLayout();

		void createDescriptorSet();
		void loadSkyBox(const std::array<std::string, 6>& textureNames, uint32_t imageSideSize, bool show = true);
		void loadTextures(const std::array<std::string, 6>& paths, int imageSideSize);
		void destroy();
	};
}