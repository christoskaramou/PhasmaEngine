#pragma once
#include "../Model/Object.h"
#include "../Renderer/Pipeline.h"

namespace vk
{
	class DescriptorSet;
	class DescriptorSetLayout;
}

namespace vm
{
	class SkyBox
	{
	public:
		SkyBox();
		~SkyBox();
		Image texture;
		Ref_t<vk::DescriptorSet> descriptorSet;
		static Ref_t<vk::DescriptorSetLayout> descriptorSetLayout;
		static const vk::DescriptorSetLayout& getDescriptorSetLayout();

		void createDescriptorSet();
		void loadSkyBox(const std::array<std::string, 6>& textureNames, uint32_t imageSideSize, bool show = true);
		void loadTextures(const std::array<std::string, 6>& paths, int imageSideSize);
		void destroy();
	};
}